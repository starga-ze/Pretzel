#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "router/TxRouter.h"

namespace nf::engined
{

class EnginedProcess : public nf::process::Process
{
public:
    EnginedProcess(nf::ipc::IpcClient* ipcClient, nf::router::TxRouter* txRouter);
    ~EnginedProcess() override = default;

    void tick() override;

private:
    nf::ipc::IpcClient* m_ipcClient;
    nf::router::TxRouter* m_txRouter;
};

} // namespace nf::engined
