#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/TopologydServiceManager.h"

namespace nf::topologyd
{

class TopologydProcess : public nf::process::Process
{
public:
    TopologydProcess(nf::ipc::IpcClient* ipcClient,
                     TopologydServiceManager* serviceManager);
    ~TopologydProcess() override = default;

    bool start() override;
    void tick() override;

private:
    nf::ipc::IpcClient*      m_ipcClient{nullptr};
    TopologydServiceManager* m_serviceManager{nullptr};
};

} // namespace nf::topologyd
