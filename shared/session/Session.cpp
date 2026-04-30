#include "session/Session.h"

#include <atomic>

namespace nf::session
{

namespace
{
std::atomic<std::uint64_t> g_sessionId {0};
}

Session::Session(int fd)
    : m_fd(fd)
    , m_id(nextId())
{
}

int Session::getFd() const noexcept
{
    return m_fd;
}

std::uint64_t Session::getId() const noexcept
{
    return m_id;
}

std::uint64_t Session::nextId() noexcept
{
    return g_sessionId.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace nf::session
