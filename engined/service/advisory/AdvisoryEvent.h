#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class AdvisoryEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveUpdate = 1,
};

// A batch of security advisories arriving from collectord, to be persisted. Same shape as
// CollectionEvent: engined is the sole DB writer, collectord makes the vendor call and hands the
// rows over by IPC.
class AdvisoryEvent final : public EnginedEvent
{
public:
    explicit AdvisoryEvent(AdvisoryEventType type);
    AdvisoryEvent(AdvisoryEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    AdvisoryEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    AdvisoryEventType m_type{AdvisoryEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
