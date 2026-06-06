#pragma once

#include "event/AuthdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::authd
{

enum class HeartbeatEventType : std::uint32_t
{
    Unknown                = 0,
    ReceiveHeartbeatRequest = 1
};

class HeartbeatEvent final : public AuthdEvent
{
public:
    explicit HeartbeatEvent(HeartbeatEventType type);

    HeartbeatEvent(HeartbeatEventType type,
                   std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(AuthdServiceManager& serviceManager) override;

    HeartbeatEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    HeartbeatEventType m_type{HeartbeatEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::authd
