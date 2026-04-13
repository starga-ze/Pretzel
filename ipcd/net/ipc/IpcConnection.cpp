#include "net/ipc/IpcConnection.h"

#include "util/Logger.h"

#include <cstring>
#include <unistd.h>

namespace nf::ipcd
{

IpcConnection::IpcConnection(int fd, std::size_t rxBufferSize, std::size_t txBufferSize)
    : m_fd(fd),
      m_rxBuffer(rxBufferSize),
      m_txBuffer(txBufferSize)
{
}

IpcConnection::~IpcConnection()
{
    close();
}

int IpcConnection::fd() const noexcept
{
    return m_fd;
}

void IpcConnection::close()
{
    if (m_fd >= 0)
    {
        LOG_INFO("IpcConnection closing fd={}", m_fd);
        ::close(m_fd);
        m_fd = -1;
    }
}

std::uint8_t* IpcConnection::rxWritePtr() noexcept
{
    return m_rxBuffer.data() + m_rxSize;
}

std::size_t IpcConnection::rxWritable() const noexcept
{
    return m_rxBuffer.size() - m_rxSize;
}

void IpcConnection::rxProduce(std::size_t n) noexcept
{
    m_rxSize += n;
    if (m_rxSize > m_rxBuffer.size())
        m_rxSize = m_rxBuffer.size();
}

std::size_t IpcConnection::rxReadable() const noexcept
{
    return m_rxSize;
}

void IpcConnection::rxConsumeAll() noexcept
{
    m_rxSize = 0;
}

const std::uint8_t* IpcConnection::txReadPtr() const noexcept
{
    return m_txBuffer.data();
}

std::size_t IpcConnection::txReadable() const noexcept
{
    return m_txSize;
}

void IpcConnection::txConsume(std::size_t n) noexcept
{
    if (n >= m_txSize)
    {
        m_txSize = 0;
        return;
    }

    std::memmove(m_txBuffer.data(), m_txBuffer.data() + n, m_txSize - n);
    m_txSize -= n;
}

bool IpcConnection::enqueueTx(const std::uint8_t* data, std::size_t len)
{
    if (!data || len == 0)
        return true;

    if (m_txBuffer.size() - m_txSize < len)
        return false;

    std::memcpy(m_txBuffer.data() + m_txSize, data, len);
    m_txSize += len;
    return true;
}

} // namespace nf::ipcd
