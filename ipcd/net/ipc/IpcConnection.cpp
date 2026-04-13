#include "net/ipc/IpcConnection.h"

#include "util/Logger.h"

#include <unistd.h>

namespace nf::ipcd
{

IpcConnection::IpcConnection(int fd)
    : m_fd(fd)
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

} // namespace nf::ipcd
