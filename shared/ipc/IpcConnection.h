#pragma once

#include "algorithm/ByteRingBuffer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pz::ipc
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

    pz::algorithm::ByteRingBuffer& rx() noexcept { return m_rx; }
    pz::algorithm::ByteRingBuffer& tx() noexcept { return m_tx; }
    const pz::algorithm::ByteRingBuffer& rx() const noexcept { return m_rx; }
    const pz::algorithm::ByteRingBuffer& tx() const noexcept { return m_tx; }

    IoResult recv(int& outErrno);

    IoResult send(int& outErrno);

    bool write(const std::uint8_t* data, std::size_t len);
    bool write(const std::vector<std::uint8_t>& data);

private:
    int m_fd{-1};
    pz::algorithm::ByteRingBuffer m_rx;
    pz::algorithm::ByteRingBuffer m_tx;
};

} // namespace pz::ipc
