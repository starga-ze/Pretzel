#pragma once

#include "event/ScandEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::scand
{

enum class HeartbeatEventType : std::uint32_t
{
    Unknown                 = 0,
    ReceiveHeartbeatRequest = 1
};

class HeartbeatEvent final : public ScandEvent
{
public:
    explicit HeartbeatEvent(HeartbeatEventType type);

    HeartbeatEvent(HeartbeatEventType type,
                   std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(ScandServiceManager& serviceManager) override;

    HeartbeatEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    HeartbeatEventType m_type{HeartbeatEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::scand
