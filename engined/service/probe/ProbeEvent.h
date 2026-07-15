#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class ProbeEventType : std::uint32_t
{
    Unknown = 0,
    TriggerProbe = 1,
    ReceiveProbeResult = 2,
};

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

}
