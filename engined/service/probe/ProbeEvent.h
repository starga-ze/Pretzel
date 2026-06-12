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
    ReceiveProbeResult = 1,
};

// Carries an icmpd ProbeResult that engined relays on to mgmtd. engined sits on the
// path purely as the control-plane hub (relay + awareness) — it logs/sequences the
// flow but does not own the data.
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
