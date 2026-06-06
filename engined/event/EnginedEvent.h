#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace nf::engined
{

class EnginedServiceManager;

enum class EnginedEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class EnginedEvent : public nf::event::Event
{
public:
    explicit EnginedEvent(EnginedEventDomain domain);
    ~EnginedEvent() override = default;

    EnginedEventDomain domain() const;

    virtual void dispatch(EnginedServiceManager& serviceManager) = 0;

private:
    EnginedEventDomain m_domain{EnginedEventDomain::Unknown};
};

} // namespace nf::engined
