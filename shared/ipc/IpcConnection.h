#pragma once

#include "algorithm/ByteRingBuffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nf::ipc
{

enum class IoResult
{
    Ok,
    WouldBlock,
    PeerClosed,
    BufferFull,
    Error
};

class IpcConnection
{
public:
    IpcConnection(int fd, std::size_t rxBuf, std::size_t txBuf);
    ~IpcConnection() = default;

    int fd() const noexcept { return m_fd; }

    nf::algorithm::ByteRingBuffer& rx() noexcept { return m_rx; }
    nf::algorithm::ByteRingBuffer& tx() noexcept { return m_tx; }
    const nf::algorithm::ByteRingBuffer& rx() const noexcept { return m_rx; }
    const nf::algorithm::ByteRingBuffer& tx() const noexcept { return m_tx; }

    // Read as much as possible from socket into rx ring.
    // Ok         : read completed and ring may contain data
    // WouldBlock : no more data for now
    // PeerClosed : peer closed connection
    // BufferFull : rx ring has no writable space
    // Error      : fatal socket error (outErrno set)
    IoResult recv(int& outErrno);

    // Flush as much as possible from tx ring to socket.
    // Ok         : tx ring fully flushed
    // WouldBlock : socket send buffer full, keep EPOLLOUT
    // PeerClosed : peer closed connection
    // Error      : fatal socket error (outErrno set)
    IoResult send(int& outErrno);

    bool write(const std::uint8_t* data, std::size_t len);
    bool write(const std::vector<std::uint8_t>& data);

private:
    int m_fd{-1};
    nf::algorithm::ByteRingBuffer m_rx;
    nf::algorithm::ByteRingBuffer m_tx;
};

} // namespace nf::ipc
