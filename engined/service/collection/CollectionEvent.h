#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class CollectionEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveSample = 1,
};

// An api_collection sample arriving from scand — one connector's scheduled endpoint poll result,
// to be persisted. Same shape as ApiKeyEvent: engined is the sole DB writer, scand hands the row
// over by IPC.
class CollectionEvent final : public EnginedEvent
{
public:
    explicit CollectionEvent(CollectionEventType type);
    CollectionEvent(CollectionEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    CollectionEventType type() const;
    const pz::ipc::IpcMessage* message() const;

private:
    CollectionEventType m_type{CollectionEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
