#pragma once

#include "event/IcmpdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::icmpd
{

class IcmpdServiceManager;

enum class BootstrapEventType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    ReceiveServerHello = 2,
    SendRuntimeReady = 3,
    ReceiveRuntimeStart = 4,
};

class BootstrapEvent final : public IcmpdEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(
        BootstrapEventType type,
        std::unique_ptr<nf::ipc::IpcMessage> message);

    BootstrapEventType type() const;

    void dispatch(IcmpdServiceManager& serviceManager) override;

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
};

} // namespace nf::icmpd
