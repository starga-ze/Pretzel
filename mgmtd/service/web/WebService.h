#pragma once

#include "http/HttpMessage.h"
#include "http/StaticFileCache.h"

#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

class MgmtdServiceManager;
class WebEvent;
class WebAction;

class WebService
{
public:
    WebService() = default;

    void setCache(std::shared_ptr<pz::http::StaticFileCache> cache);

    void handleEvent(MgmtdServiceManager& serviceManager, const WebEvent& event);

    void handleAction(MgmtdServiceManager& serviceManager, WebAction& action);

private:
    using Request = pz::http::HttpRequest;
    using Response = pz::http::HttpResponse;

    void route(MgmtdServiceManager& sm, const Request& req, Response& resp);

    void handleMetric(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleHealth(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLogin(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLogout(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleChangePassword(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleWhoami(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleStatus(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSettingsGet(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSettingsCommit(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleReloadStatus(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleCommitQueue(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleInventoryStatus(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLogs(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleNodeMetrics(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleLabExport(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleStatic(MgmtdServiceManager& sm, const Request& req, Response& resp);

    void handleSsoInfo(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSsoLogin(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSamlAcs(MgmtdServiceManager& sm, const Request& req, Response& resp);
    void handleSamlResult(MgmtdServiceManager& sm, const Request& req, Response& resp);

    bool isAuthenticated(MgmtdServiceManager& sm, const Request& req) const;

    static bool isStaticTarget(const std::string& target);
    static std::string extractSession(const Request& req);

    std::shared_ptr<pz::http::StaticFileCache> m_cache;

    std::uint32_t m_ssoTicket{1};
};

}
