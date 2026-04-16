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

    IoResult recv(int& outErrno);

    IoResult send(int& outErrno);

    bool write(const std::uint8_t* data, std::size_t len);
    bool write(const std::vector<std::uint8_t>& data);

private:
    int m_fd{-1};
    nf::algorithm::ByteRingBuffer m_rx;
    nf::algorithm::ByteRingBuffer m_tx;
};

} // namespace nf::ipc
