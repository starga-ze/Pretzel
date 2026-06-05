#pragma once

#include "process/Process.h"

namespace nf::ipc
{
class IpcClient;
}

namespace nf::mgmtd
{

class HttpServer;
class MgmtdServiceManager;

class MgmtdProcess : public nf::process::Process
{
public:
    MgmtdProcess(nf::ipc::IpcClient* ipcClient,
                 HttpServer* httpServer,
                 MgmtdServiceManager* serviceManager);
    ~MgmtdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    nf::ipc::IpcClient* m_ipcClient {nullptr};
    HttpServer* m_httpServer {nullptr};
    MgmtdServiceManager* m_serviceManager {nullptr};
};

} // namespace nf::mgmtd
