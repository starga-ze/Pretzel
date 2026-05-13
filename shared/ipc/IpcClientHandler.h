#pragma once

#include "ipc/IpcHandler.h"

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

private:
    IpcClient* m_ipcClient;
};

} // namespace nf::ipc
