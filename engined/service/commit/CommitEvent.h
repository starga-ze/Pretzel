#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class CommitEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveSettingsCommit = 1,
    ReloadComplete = 2,
    ReloadFailed = 3,
};

class CommitEvent final : public EnginedEvent
{
public:
    explicit CommitEvent(CommitEventType type);
    CommitEvent(CommitEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    CommitEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    CommitEventType m_type{CommitEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
