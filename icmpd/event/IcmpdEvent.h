#pragma once

#include "event/Event.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::icmpd
{

class IcmpdServiceManager;

enum class IcmpdEventDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Probe = 2
};

class IcmpdEvent : public nf::event::Event
{
public:
    explicit IcmpdEvent(IcmpdEventDomain domain);

    IcmpdEvent(IcmpdEventDomain domain, std::unique_ptr<nf::ipc::IpcMessage> message);

    ~IcmpdEvent() override = default;

    IcmpdEventDomain domain() const;

    const nf::ipc::IpcMessage* message() const;
    std::unique_ptr<nf::ipc::IpcMessage> takeMessage();

    virtual void dispatch(IcmpdServiceManager& serviceManager) = 0;

private:
    IcmpdEventDomain m_domain{IcmpdEventDomain::Unknown};
    std::unique_ptr<nf::ipc::IpcMessage> m_message;
};

} // namespace nf::icmpd
