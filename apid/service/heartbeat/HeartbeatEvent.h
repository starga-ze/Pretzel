#pragma once

#include "event/ApidEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::apid
{

enum class HeartbeatEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveHeartbeatRequest = 1
};

class HeartbeatEvent final : public ApidEvent
{
public:
    explicit HeartbeatEvent(HeartbeatEventType type);

    HeartbeatEvent(HeartbeatEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(ApidServiceManager& serviceManager) override;

    HeartbeatEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    HeartbeatEventType m_type{HeartbeatEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
