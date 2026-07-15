#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/AuthdServiceManager.h"

#include <chrono>

namespace pz::authd
{

class AuthdProcess : public pz::process::Process
{
public:
    AuthdProcess(pz::ipc::IpcClient* ipcClient, AuthdServiceManager* serviceManager);
    ~AuthdProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    AuthdServiceManager* m_serviceManager;
};

}
