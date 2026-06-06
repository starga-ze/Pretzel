#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/EnginedServiceManager.h"

namespace nf::engined
{

class EnginedProcess : public nf::process::Process
{
public:
    EnginedProcess(nf::ipc::IpcClient* ipcClient, EnginedServiceManager* serviceManager);
    ~EnginedProcess() override = default;

    bool start() override;
    void tick() override;

private:
    nf::ipc::IpcClient* m_ipcClient{nullptr};
    EnginedServiceManager* m_serviceManager{nullptr};
};

} // namespace nf::engined
