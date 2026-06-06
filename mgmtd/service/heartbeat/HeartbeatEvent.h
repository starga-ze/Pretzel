#pragma once

#include "event/MgmtdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

enum class HeartbeatEventType : std::uint32_t
{
    Unknown              = 0,
    ReceiveHeartbeatResult = 1
};

class HeartbeatEvent final : public MgmtdEvent
{
public:
    explicit HeartbeatEvent(HeartbeatEventType type);

    HeartbeatEvent(HeartbeatEventType type,
                   std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    HeartbeatEventType type() const;
    const nf::ipc::IpcMessage* message() const;

private:
    HeartbeatEventType m_type{HeartbeatEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::mgmtd
