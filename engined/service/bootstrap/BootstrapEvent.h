#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::engined
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown              = 0,
    SendClientHello      = 1,
    ReceiveServerHello   = 2,
    SendSyncRequest      = 3,
    ReceiveSyncResponse  = 4,
    SendRuntimeStart     = 5,
    ReceiveRuntimeStart  = 6
};

class BootstrapEvent final : public EnginedEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type,
                          std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::engined
