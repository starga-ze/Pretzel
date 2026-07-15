#pragma once

#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"
#include "router/TxRouter.h"

#include <memory>

namespace pz::topologyd
{

class TopologydTxRouter : public pz::router::TxRouter
{
public:
    explicit TopologydTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler);

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler{nullptr};
};

}
