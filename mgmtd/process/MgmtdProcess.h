#pragma once

#include "process/Process.h"

namespace pz::ipc
{
class IpcClient;
}

namespace pz::mgmtd
{

class HttpServer;
class MgmtdServiceManager;

class MgmtdProcess : public pz::process::Process
{
public:
    MgmtdProcess(pz::ipc::IpcClient* ipcClient,
                 HttpServer* httpServer,
                 MgmtdServiceManager* serviceManager);
    ~MgmtdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient {nullptr};
    HttpServer* m_httpServer {nullptr};
    MgmtdServiceManager* m_serviceManager {nullptr};
};

} // namespace pz::mgmtd
