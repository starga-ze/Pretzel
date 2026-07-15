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

namespace pz::mgmtd
{

class MgmtdServiceManager;

class MgmtdProcess : public pz::process::Process
{
public:
    MgmtdProcess(pz::ipc::IpcClient* ipcClient, pz::http::HttpServer* httpServer, MgmtdServiceManager* serviceManager);
    ~MgmtdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient{nullptr};
    pz::http::HttpServer* m_httpServer{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

}
