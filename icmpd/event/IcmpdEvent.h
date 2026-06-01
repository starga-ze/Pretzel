#pragma once

#include "event/Event.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::icmpd
{

enum class IcmpdEventType : std::uint32_t
{
    Unknown             = 0,

    SendClientHello     = 1,
    ReceiveServerHello  = 2,

    PeriodicScanDue,
    ManualScanRequested,
};

class IcmpdEvent final : public nf::event::Event
{
public:
    explicit IcmpdEvent(IcmpdEventType type);

    IcmpdEvent(IcmpdEventType type, std::unique_ptr<nf::ipc::IpcMessage> message);

    ~IcmpdEvent() override = default;

    IcmpdEventType type() const;

    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

private:
    IcmpdEventType m_type {IcmpdEventType::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::icmpd
