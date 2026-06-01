#pragma once

#include "event/IcmpdEvent.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::icmpd
{

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
    explicit BootstrapEvent(BootstrapEventType type)
        : IcmpdEvent(IcmpdEventDomain::Bootstrap),
          m_type(type)
    {
    }

    BootstrapEvent(BootstrapEventType type,
                   std::unique_ptr<nf::ipc::IpcMessage> message)
        : IcmpdEvent(IcmpdEventDomain::Bootstrap),
          m_type(type),
          m_message(std::move(message))
    {
    }

    BootstrapEventType type() const
    {
        return m_type;
    }

    const nf::ipc::IpcMessage* message() const
    {
        return m_message.get();
    }

    std::unique_ptr<nf::ipc::IpcMessage> takeMessage()
    {
        return std::move(m_message);
    }


private:
    BootstrapEventType m_type{BootstrapEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::icmpd
