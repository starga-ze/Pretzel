#pragma once

#include "event/EnginedEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::engined
{

enum class EnginedBootstrapEventType : std::uint32_t
{
    Unknown              = 0,
    SendClientHello      = 1,
    ReceiveServerHello   = 2,
    SendSyncRequest      = 3,
    ReceiveSyncResponse  = 4,
    SendRuntimeStart     = 5,
    ReceiveRuntimeStart  = 6
};

class EnginedBootstrapEvent final : public EnginedEvent
{
public:
    explicit EnginedBootstrapEvent(EnginedBootstrapEventType type);

    EnginedBootstrapEvent(EnginedBootstrapEventType type,
                          std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(EnginedServiceManager& serviceManager) override;

    EnginedBootstrapEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    EnginedBootstrapEventType m_type{EnginedBootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::engined
