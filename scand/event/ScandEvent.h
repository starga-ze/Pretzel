#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::scand
{

class ScandServiceManager;

enum class ScandEventDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Heartbeat = 2,
    Scan = 3,
    Reload = 4,
};

class ScandEvent : public pz::event::Event
{
public:
    explicit ScandEvent(ScandEventDomain domain);
    ~ScandEvent() override = default;

    ScandEventDomain domain() const;

    virtual void dispatch(ScandServiceManager& serviceManager) = 0;

private:
    ScandEventDomain m_domain{ScandEventDomain::Unknown};
};

}
