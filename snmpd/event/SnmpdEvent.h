#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::snmpd
{

class SnmpdServiceManager;

enum class SnmpdEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class SnmpdEvent : public pz::event::Event
{
public:
    explicit SnmpdEvent(SnmpdEventDomain domain);
    ~SnmpdEvent() override = default;

    SnmpdEventDomain domain() const;

    virtual void dispatch(SnmpdServiceManager& serviceManager) = 0;

private:
    SnmpdEventDomain m_domain{SnmpdEventDomain::Unknown};
};

} // namespace pz::snmpd
