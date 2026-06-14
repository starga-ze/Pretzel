#include "service/admin/AdminService.h"

#include "service/EnginedServiceManager.h"

#include "db/Database.h"
#include "ipc/IpcMessage.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::engined
{

void AdminService::handleEvent(EnginedServiceManager& /*serviceManager*/,
                               const AdminEvent& event)
{
    if (event.type() != AdminEventType::ReceivePasswordUpdate)
    {
        return;
    }

    const pz::ipc::IpcMessage* in = event.message();
    if (!in || in->getPayload().empty())
    {
        LOG_WARN("AdminService: empty AdminPasswordUpdate — dropping");
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
        LOG_WARN("AdminService: failed to parse AdminPasswordUpdate payload: {}", e.what());
        return;
    }

    const std::string username = root.value("username", "");
    const std::string hash     = root.value("password_hash", "");
    const std::string salt     = root.value("salt", "");

    if (username.empty() || hash.empty() || salt.empty())
    {
        LOG_WARN("AdminService: AdminPasswordUpdate missing fields — dropping");
        return;
    }

    // Credentials live in local_users (a non-versioned store), so a password change is
    // a single-row UPDATE — it does NOT create a running_config version. must_change is
    // cleared (the operator moved off the factory default). No ConfigReload: the sole
    // consumer (mgmtd) updates its in-memory credential directly.
    const bool ok = pz::db::Database::instance().exec(
        "UPDATE local_users SET password_hash = $1, salt = $2, "
        "must_change = false, updated_at = now() WHERE username = $3",
        {hash, salt, username});

    if (ok)
        LOG_INFO("AdminService: password updated for user '{}' (local_users)", username);
    else
        LOG_WARN("AdminService: local_users update failed for user '{}'", username);
}

} // namespace pz::engined
