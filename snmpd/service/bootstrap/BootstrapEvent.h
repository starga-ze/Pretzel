#pragma once

#include "event/SnmpdEvent.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::snmpd
{

enum class BootstrapEventType : std::uint32_t
{
    Unknown             = 0,
    SendClientHello     = 1,
    ReceiveServerHello  = 2,
    SendRuntimeReady    = 3,
    ReceiveRuntimeStart = 4
};

class BootstrapEvent final : public SnmpdEvent
{
public:
    explicit BootstrapEvent(BootstrapEventType type);

    BootstrapEvent(BootstrapEventType type,
                   std::unique_ptr<nf::ipc::IpcMessage> message);

    void dispatch(SnmpdServiceManager& serviceManager) override;

    BootstrapEventType type() const;
    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::snmpd
