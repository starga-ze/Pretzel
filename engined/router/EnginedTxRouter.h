#pragma once

#include "router/TxRouter.h"

namespace nf::ipc
{
class IpcClientHandler;
}

namespace nf::engined
{

class EnginedTxRouter : public nf::router::TxRouter
{
public:
    explicit EnginedTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~EnginedTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler{nullptr};
};

} // namespace nf::engined
