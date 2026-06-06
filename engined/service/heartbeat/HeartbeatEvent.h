#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <cstdint>
#include <memory>

namespace nf::engined
{

enum class HeartbeatEventType : std::uint32_t
{
    Unknown                  = 0,
    SendHeartbeatRequests    = 1,
    ReceiveHeartbeatResponse = 2,
    SendHeartbeatResult      = 3
};

class HeartbeatEvent final : public EnginedEvent
{
public:
    explicit HeartbeatEvent(HeartbeatEventType type);

    HeartbeatEvent(HeartbeatEventType type,
                   std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    HeartbeatEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    HeartbeatEventType m_type{HeartbeatEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::engined
