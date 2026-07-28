#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::collectord
{

class CollectordServiceManager;

enum class CollectordEventDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Heartbeat = 2,
    Scan = 3,
    Reload = 4,
    Api = 5,
};

class CollectordEvent : public pz::event::Event
{
public:
    explicit CollectordEvent(CollectordEventDomain domain);
    ~CollectordEvent() override = default;

    CollectordEventDomain domain() const;

    virtual void dispatch(CollectordServiceManager& serviceManager) = 0;

private:
    CollectordEventDomain m_domain{CollectordEventDomain::Unknown};
};

}
