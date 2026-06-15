#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class ProbeEventType : std::uint32_t
{
    Unknown            = 0,
    TriggerProbe       = 1,  // timer: ask icmpd to run one probe cycle
    ReceiveProbeResult = 2,  // icmpd → engined: alive-IP snapshot
};

// Drives the engined-owned ICMP probe orchestration. TriggerProbe is timer-emitted;
// ReceiveProbeResult carries the ProbeResult IPC message from icmpd.
class ProbeEvent final : public EnginedEvent
{
public:
    explicit ProbeEvent(ProbeEventType type);
    ProbeEvent(ProbeEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    ProbeEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    ProbeEventType m_type{ProbeEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::engined
