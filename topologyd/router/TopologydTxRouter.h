#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::topologyd
{

class TopologydTxRouter : public nf::router::TxRouter
{
public:
    explicit TopologydTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler{nullptr};
};

} // namespace nf::topologyd
