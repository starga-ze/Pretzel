#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class CommitEventType : std::uint32_t
{
    Unknown               = 0,
    ReceiveSettingsCommit = 1,
    ReloadComplete        = 2,  // fired by BootstrapService after a reload converges
    ReloadFailed          = 3,  // fired by BootstrapService when a reload times out
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

} // namespace pz::engined
