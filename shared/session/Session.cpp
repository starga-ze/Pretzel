#include "session/Session.h"

namespace pz::session
{

Session::Session(std::string id,
                 pz::ipc::IpcDaemon src,
                 pz::ipc::IpcDaemon dst)
    : m_id(std::move(id)),
      m_src(src),
      m_dst(dst)
{
}

const std::string& Session::getId() const noexcept
{
    return m_id;
}

pz::ipc::IpcDaemon Session::getSrc() const noexcept
{
    return m_src;
}

pz::ipc::IpcDaemon Session::getDst() const noexcept
{
    return m_dst;
}

SessionState Session::getState() const noexcept
{
    return m_state;
}

bool Session::isEstablished() const noexcept
{
    return m_state == SessionState::Established;
}

bool Session::isClosing() const noexcept
{
    return m_state == SessionState::Closing;
}

std::uint64_t Session::getRxCount() const noexcept
{
    return m_rxCount;
}

std::uint64_t Session::getTxCount() const noexcept
{
    return m_txCount;
}

std::uint32_t Session::getLastSeqNo() const noexcept
{
    return m_lastSeqNo;
}

void Session::markEstablished() noexcept
{
    m_state = SessionState::Established;
}

void Session::markClosing() noexcept
{
    m_state = SessionState::Closing;
}

void Session::markRx(std::uint32_t seqNo) noexcept
{
    ++m_rxCount;
    m_lastSeqNo = seqNo;
}

void Session::markTx() noexcept
{
    ++m_txCount;
}

} // namespace pz::session
