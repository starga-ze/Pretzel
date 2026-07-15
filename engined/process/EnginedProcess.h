#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

class EnginedProcess : public pz::process::Process
{
public:
    EnginedProcess(pz::ipc::IpcClient* ipcClient, EnginedServiceManager* serviceManager);
    ~EnginedProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient* m_ipcClient{nullptr};
    EnginedServiceManager* m_serviceManager{nullptr};
};

}
