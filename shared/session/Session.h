#pragma once

#include "ipc/IpcProtocol.h"

#include <cstdint>
#include <string>
#include <utility>

namespace pz::session
{

enum class SessionState : std::uint8_t
{
    Init = 0,
    Established,
    Closing
};

class Session
{
public:
    Session(std::string id,
            pz::ipc::IpcDaemon src,
            pz::ipc::IpcDaemon dst);

    ~Session() = default;

    const std::string& getId() const noexcept;

    pz::ipc::IpcDaemon getSrc() const noexcept;
    pz::ipc::IpcDaemon getDst() const noexcept;

    SessionState getState() const noexcept;
    bool isEstablished() const noexcept;
    bool isClosing() const noexcept;

    std::uint64_t getRxCount() const noexcept;
    std::uint64_t getTxCount() const noexcept;
    std::uint32_t getLastSeqNo() const noexcept;

    void markEstablished() noexcept;
    void markClosing() noexcept;

    void markRx(std::uint32_t seqNo) noexcept;
    void markTx() noexcept;

private:
    std::string m_id;

    pz::ipc::IpcDaemon m_src;
    pz::ipc::IpcDaemon m_dst;

    SessionState m_state { SessionState::Init };

    std::uint64_t m_rxCount { 0 };
    std::uint64_t m_txCount { 0 };
    std::uint32_t m_lastSeqNo { 0 };
};

} // namespace pz::session
