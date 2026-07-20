#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/ScandServiceManager.h"

#include <chrono>

namespace pz::scand
{

class ScandProcess : public pz::process::Process
{
public:
    ScandProcess(pz::ipc::IpcClient* ipcClient, ScandServiceManager* serviceManager);
    ~ScandProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient;
    ScandServiceManager* m_serviceManager;
};

}
