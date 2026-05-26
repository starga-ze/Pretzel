#pragma once

#include "router/RxRouter.h"

#include "router/EnginedTxRouter.h"
#include "process/EnginedProcess.h"

namespace nf::engined
{

class EnginedRxRouter : public nf::router::RxRouter
{
public:
    EnginedRxRouter(EnginedTxRouter* txRouter);
    ~EnginedRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setProcess(EnginedProcess* process);

private:
    EnginedProcess* m_process;
    EnginedTxRouter* m_txRouter;
};
} // namespace nf::engined
