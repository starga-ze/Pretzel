#pragma once

#include "process/Process.h"

#include "ipc/IpcServer.h"
#include "service/IpcdServiceManager.h"

namespace nf::ipcd
{

class IpcdProcess : public nf::process::Process
{
public:
    IpcdProcess(IpcServer* ipcServer, IpcdServiceManager* serviceManager);
    ~IpcdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    IpcServer* m_ipcServer{nullptr};
    IpcdServiceManager* m_serviceManager{nullptr};
};

} // namespace nf::ipcd
