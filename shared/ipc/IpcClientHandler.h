#pragma once

#include "ipc/IpcHandler.h"

namespace nf::ipc
{

class IpcClientHandler : public IpcHandler
{
public:
    IpcClientHandler() = default;
    ~IpcClientHandler() override = default;

    bool handleConnect(int fd, nf::io::Epoll& epoll);
    bool handleRecv(int fd, IpcConnection& conn, nf::io::Epoll& epoll);
    bool handleSend(int fd, IpcConnection& conn, nf::io::Epoll& epoll);

    bool ingress(int fd, nf::ipc::IpcFrameView frame) override;
    void egress(std::unique_ptr<IpcMessage> msg) override;
};

} // namespace nf::ipc
