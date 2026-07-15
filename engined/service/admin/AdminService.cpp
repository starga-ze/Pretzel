#include "service/admin/AdminService.h"

#include "service/EnginedServiceManager.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::engined
{

void AdminService::handleEvent(EnginedServiceManager&, const AdminEvent& event)
{
    if (event.type() != AdminEventType::ReceivePasswordUpdate)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
    if (!in || in->getPayload().empty())
    {
        LOG_WARN("empty AdminPasswordUpdate — dropping");
        return;
    }

    const auto& pl = in->getPayload();
    updatePassword(std::string(reinterpret_cast<const char*>(pl.data()), pl.size()));
}

void AdminService::updatePassword(const std::string& payloadJson)
{
    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(payloadJson);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("failed to parse AdminPasswordUpdate payload (error={})", e.what());
        return;
    }

    const std::string username = root.value("username", "");
    const std::string hash = root.value("password_hash", "");
    const std::string salt = root.value("salt", "");

    if (username.empty() || hash.empty() || salt.empty())
    {
        LOG_WARN("AdminPasswordUpdate missing fields — dropping");
        return;
    }

    const bool ok = pz::db::Database::instance().exec("UPDATE local_users SET password_hash = $1, salt = $2, "
                                                      "must_change = false, updated_at = now() WHERE username = $3",
                                                      {hash, salt, username});

    if (ok)
        LOG_INFO("password updated in local_users (user={})", username);
    else
        LOG_WARN("local_users update failed (user={})", username);
}

}
