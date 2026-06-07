#pragma once

#include "io/Epoll.h"
#include "ipc/IpcCodec.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcMessage.h"

#include <cstdint>

namespace pz::ipc
{

class IpcHandler
{
public:
    IpcHandler() = default;
    virtual ~IpcHandler() = default;

    bool handleRecv(int fd, IpcConnection& conn, pz::io::Epoll& epoll);
    bool handleSend(int fd, IpcConnection& conn, pz::io::Epoll& epoll);

    virtual bool ingress(int fd, pz::ipc::IpcFrameView frame) = 0;
    virtual void egress(std::unique_ptr<IpcMessage> msg) = 0;

private:
    bool drainRxFrames(int fd, IpcConnection& conn);

protected:
    IpcCodec m_codec;
};

} // namespace pz::ipc
