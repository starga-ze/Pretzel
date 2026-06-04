#pragma once

#include "router/RxRouter.h"

#include "router/EnginedTxRouter.h"
#include "process/EnginedProcess.h"

namespace nf::engined
{

class EnginedRxRouter : public nf::router::RxRouter
{
public:
    EnginedRxRouter(nf::ipc::IpcClientHandler* ipcClientHandler, EnginedTxRouter* txRouter);
    ~EnginedRxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    void handleEvent(std::unique_ptr<nf::event::Event> event);

    void setProcess(EnginedProcess* process);

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler = nullptr;
    EnginedProcess* m_process = nullptr;
    EnginedTxRouter* m_txRouter = nullptr;
};
} // namespace nf::engined
