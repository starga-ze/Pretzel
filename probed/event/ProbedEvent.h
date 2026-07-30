#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::probed
{

class ProbedServiceManager;

enum class ProbedEventDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Icmp = 2,
    Heartbeat = 3,
    Reload = 4
};

class ProbedEvent : public pz::event::Event
{
public:
    explicit ProbedEvent(ProbedEventDomain domain);
    ~ProbedEvent() override = default;

    ProbedEventDomain domain() const;

    virtual void dispatch(ProbedServiceManager& serviceManager) = 0;

private:
    ProbedEventDomain m_domain{ProbedEventDomain::Unknown};
};

}
