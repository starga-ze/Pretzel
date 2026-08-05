#pragma once

#include "http/HttpMessage.h"

namespace pz::ipc
{
class IpcMessage;
}

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Configuration surface: GET /api/settings, GET /api/settings/running-config,
// POST /api/settings/commit, GET /api/settings/reload-status, GET /api/settings/commit-queue,
// POST /api/settings/save-config, GET /api/settings/saved-configs, GET /api/settings/saved-config-content.
// Reads the active running-config for the editors and validates a commit's per-domain schema before
// forwarding it to engined. An instance owned by WebService, reached from its dispatch switch by
// member call; the commit schema and saved-config helpers stay private to the .cpp.
class SettingsController
{
public:
    void get(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void runningConfig(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void commit(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void reloadStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void commitQueue(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void saveConfig(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void savedConfigs(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void savedConfigContent(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // engined's commit queue moved. The snapshot is what GET /api/settings/commit-queue serves, so
    // filing it belongs beside that route rather than in the router that happened to receive it.
    void onCommitStatus(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);
};

}
