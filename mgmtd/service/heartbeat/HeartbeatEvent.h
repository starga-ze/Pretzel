#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::mgmtd
{

enum class HeartbeatEventType : std::uint32_t
{
    Unknown                = 0,
    ReceiveHeartbeatResult  = 1,
    ReceiveHeartbeatRequest = 2
};

class HeartbeatEvent final : public MgmtdEvent
{
public:
    explicit HeartbeatEvent(HeartbeatEventType type);

    HeartbeatEvent(HeartbeatEventType type,
                   std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    HeartbeatEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    HeartbeatEventType m_type{HeartbeatEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::mgmtd
