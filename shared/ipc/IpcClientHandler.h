#pragma once

#include "ipc/IpcHandler.h"

#include "router/RxRouter.h"
#include "router/TxRouter.h"

namespace pz::ipc
{

class IpcClient;

class IpcClientHandler : public IpcHandler
{
public:
    IpcClientHandler(IpcClient* ipcClient);
    ~IpcClientHandler() override = default;

    bool handleConnect(int fd, pz::io::Epoll& epoll);
    bool handleRecv(int fd, IpcConnection& conn, pz::io::Epoll& epoll);
    bool handleSend(int fd, IpcConnection& conn, pz::io::Epoll& epoll);

    bool ingress(int fd, pz::ipc::IpcFrameView frame) override;
    void egress(std::unique_ptr<IpcMessage> msg) override;

    void setRxRouter(pz::router::RxRouter* rxRouter);

private:
    IpcClient* m_ipcClient = nullptr;
    pz::router::RxRouter* m_rxRouter = nullptr;
};

}
