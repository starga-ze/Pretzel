#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::icmpd
{

class IcmpdServiceManager;

enum class IcmpdEventDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Probe = 2,
    Heartbeat = 3,
    Reload = 4
};

class IcmpdEvent : public pz::event::Event
{
public:
    explicit IcmpdEvent(IcmpdEventDomain domain);
    ~IcmpdEvent() override = default;

    IcmpdEventDomain domain() const;

    virtual void dispatch(IcmpdServiceManager& serviceManager) = 0;

private:
    IcmpdEventDomain m_domain{IcmpdEventDomain::Unknown};
};

} // namespace pz::icmpd
