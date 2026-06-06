#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace nf::topologyd
{

class TopologydServiceManager;

enum class TopologydEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class TopologydEvent : public nf::event::Event
{
public:
    explicit TopologydEvent(TopologydEventDomain domain);
    ~TopologydEvent() override = default;

    TopologydEventDomain domain() const;

    virtual void dispatch(TopologydServiceManager& serviceManager) = 0;

private:
    TopologydEventDomain m_domain{TopologydEventDomain::Unknown};
};

} // namespace nf::topologyd
