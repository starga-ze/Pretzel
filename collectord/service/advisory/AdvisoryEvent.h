#pragma once

#include "event/CollectordEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::collectord
{

enum class AdvisoryEventType : std::uint32_t
{
    Unknown = 0,
    // Schedule-driven (injected by AdvisoryService::schedule, not from the wire): one-shot, arms
    // the collector once bootstrap has made the IPC client live.
    Setup = 1,
    // Inbound IPC — mgmtd asking for a collection ahead of the interval.
    RunRefresh = 2,
};

class AdvisoryEvent final : public CollectordEvent
{
public:
    explicit AdvisoryEvent(AdvisoryEventType type);
    AdvisoryEvent(AdvisoryEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(CollectordServiceManager& serviceManager) override;

    AdvisoryEventType type() const;

private:
    AdvisoryEventType m_type{AdvisoryEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
