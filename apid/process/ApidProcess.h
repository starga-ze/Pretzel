#pragma once

#include "process/Process.h"

namespace pz::ipc
{
class IpcClient;
}

namespace pz::http
{
class HttpServer;
}

namespace pz::apid
{

class ApidServiceManager;

class ApidProcess : public pz::process::Process
{
public:
    ApidProcess(pz::ipc::IpcClient* ipcClient,
                pz::http::HttpServer* httpServer,
                ApidServiceManager* serviceManager);
    ~ApidProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient {nullptr};
    pz::http::HttpServer* m_httpServer {nullptr};
    ApidServiceManager* m_serviceManager {nullptr};
};

} // namespace pz::apid
