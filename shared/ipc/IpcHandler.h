#pragma once

#include "io/Epoll.h"
#include "ipc/IpcCodec.h"
#include "ipc/IpcConnection.h"
#include "ipc/IpcMessage.h"

#include <cstdint>

namespace nf::ipc
{

class IpcHandler
{
public:
    IpcHandler() = default;
    virtual ~IpcHandler() = default;

    bool handleRecv(int fd, IpcConnection& conn, nf::io::Epoll& epoll);
    bool handleSend(int fd, IpcConnection& conn, nf::io::Epoll& epoll);

    virtual void onRxMessage(int fd, std::unique_ptr<IpcMessage> msg) = 0;
    virtual void onTxMessage(std::unique_ptr<IpcMessage> msg) = 0;

private:
    bool drainRxFrames(int fd, IpcConnection& conn);

protected:
    IpcCodec m_codec;
};

} // namespace nf::ipc
