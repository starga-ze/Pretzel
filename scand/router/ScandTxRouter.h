#pragma once

#include "ipc/IpcClientHandler.h"
#include "router/TxRouter.h"

namespace pz::scand
{

class ScandTxRouter : public pz::router::TxRouter
{
public:
    explicit ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);
    ~ScandTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
};

}
