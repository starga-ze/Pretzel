#pragma once

#include "event/IpcdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::ipcd
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown = 0,
    ReceiveClientHello = 1,
    ReceiveSyncRequest = 2,
    ReceiveRuntimeReady = 3
};

class BootstrapEvent final : public IpcdEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type, std::unique_ptr<pz::ipc::IpcMessage> message);

    void dispatch(IpcdServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const pz::ipc::IpcMessage* message() const;
    std::unique_ptr<pz::ipc::IpcMessage> takeMessage();

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<pz::ipc::IpcMessage> m_message;
};

}
