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
    EnginedTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler);
    ~EnginedTxRouter() override = default;

    void handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;
    
    void sendClientHello();

private:
    nf::ipc::IpcClientHandler* m_ipcClientHandler;
};

} // namespace nf::engined
