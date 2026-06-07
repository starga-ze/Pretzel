#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "service/TopologydServiceManager.h"

namespace pz::topologyd
{

class TopologydProcess : public pz::process::Process
{
public:
    TopologydProcess(pz::ipc::IpcClient* ipcClient,
                     TopologydServiceManager* serviceManager);
    ~TopologydProcess() override = default;

    bool start() override;
    void tick() override;

private:
    pz::ipc::IpcClient*      m_ipcClient{nullptr};
    TopologydServiceManager* m_serviceManager{nullptr};
};

} // namespace pz::topologyd
