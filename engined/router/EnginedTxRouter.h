#pragma once

#include "router/TxRouter.h"

namespace pz::ipc
{
class IpcClientHandler;
}

namespace pz::engined
{

class EnginedTxRouter : public pz::router::TxRouter
{
public:
    explicit EnginedTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);
    ~EnginedTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler{nullptr};
};

}
