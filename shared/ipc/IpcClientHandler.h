#pragma once

#include "ipc/IpcHandler.h"

#include "router/RxRouter.h"
#include "router/TxRouter.h"

namespace nf::ipc
{

class IpcClient;

class IpcClientHandler : public IpcHandler
{
public:
    IpcClientHandler(IpcClient* ipcClient);
    ~IpcClientHandler() override = default;

    bool handleConnect(int fd, nf::io::Epoll& epoll);
    bool handleRecv(int fd, IpcConnection& conn, nf::io::Epoll& epoll);
    bool handleSend(int fd, IpcConnection& conn, nf::io::Epoll& epoll);

    bool ingress(int fd, nf::ipc::IpcFrameView frame) override;
    void egress(std::unique_ptr<IpcMessage> msg) override;

    void setRxRouter(nf::router::RxRouter* rxRouter);

private:
    IpcClient* m_ipcClient = nullptr;
    nf::router::RxRouter* m_rxRouter = nullptr;
};

} // namespace nf::ipc
