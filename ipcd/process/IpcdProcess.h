#pragma once

#include "process/Process.h"

#include "ipc/IpcServer.h"
#include "service/IpcdServiceManager.h"

namespace pz::ipcd
{

class IpcdProcess : public pz::process::Process
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

} // namespace pz::ipcd
