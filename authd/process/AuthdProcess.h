#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/AuthdServiceManager.h"

#include <chrono>

namespace nf::authd
{

class AuthdProcess : public nf::process::Process
{
public:
    AuthdProcess(nf::ipc::IpcClient* ipcClient,
                 AuthdServiceManager* serviceManager);
    ~AuthdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    nf::ipc::IpcClient* m_ipcClient;
    AuthdServiceManager* m_serviceManager;
};

} // namespace nf::authd
