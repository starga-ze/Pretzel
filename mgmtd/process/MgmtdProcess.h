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
class GrpcClientHandler;

class MgmtdProcess : public pz::process::Process
{
public:
    MgmtdProcess(pz::ipc::IpcClient* ipcClient, pz::http::HttpServer* httpServer,
                 GrpcClientHandler* grpcClient, MgmtdServiceManager* serviceManager);
    ~MgmtdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient{nullptr};
    pz::http::HttpServer* m_httpServer{nullptr};
    GrpcClientHandler* m_grpcClient{nullptr};
    MgmtdServiceManager* m_serviceManager{nullptr};
};

}
