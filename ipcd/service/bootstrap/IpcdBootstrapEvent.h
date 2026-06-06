#pragma once

#include "event/IpcdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::ipcd
{

enum class IpcdBootstrapEventType : std::uint32_t
{
    Unknown              = 0,
    ReceiveClientHello   = 1,
    ReceiveSyncRequest   = 2,
    ReceiveRuntimeReady  = 3
};

class IpcdBootstrapEvent final : public IpcdEvent
{
public:
    explicit IpcdBootstrapEvent(IpcdBootstrapEventType type);

    IpcdBootstrapEvent(IpcdBootstrapEventType type,
                       std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(IpcdServiceManager& serviceManager) override;

    IpcdBootstrapEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    IpcdBootstrapEventType m_type{IpcdBootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::ipcd
