#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown              = 0,
    SendClientHello      = 1,
    ReceiveServerHello   = 2,
    SendSyncRequest      = 3,
    ReceiveSyncResponse  = 4,
    SendRuntimeStart     = 5,
    ReceiveRuntimeStart  = 6,
    ReloadFailed         = 7   // a commit-triggered reload failed to converge in time
};

class BootstrapEvent final : public EnginedEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type,
                          std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

} // namespace pz::engined
