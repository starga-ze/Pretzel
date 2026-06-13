#include "service/admin/AdminService.h"

#include "service/EnginedServiceManager.h"

#include "config/Config.h"
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

    // The admin credential lives in the running-config. Take the current root, update
    // mgmtd.service.http.admin (clearing must_change — the operator moved off the
    // factory default), and commit it as a new running_config version. No ConfigReload:
    // the sole consumer (mgmtd) updates its in-memory credential directly.
    nlohmann::json cfgRoot = pz::config::Config::runningConfigRoot();
    cfgRoot["mgmtd"]["service"]["http"]["admin"] = {
        {"username",      username},
        {"password_hash", hash},
        {"salt",          salt},
        {"must_change",   false},
    };

    if (pz::config::Config::commitConfig(cfgRoot))
        LOG_INFO("AdminService: admin password updated for user '{}' (running_config)", username);
    else
        LOG_WARN("AdminService: admin credential commit failed for user '{}'", username);
}

} // namespace pz::engined
