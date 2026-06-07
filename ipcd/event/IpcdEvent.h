#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::ipcd
{

class IpcdServiceManager;

enum class IpcdEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1
};

class IpcdEvent : public pz::event::Event
{
public:
    explicit IpcdEvent(IpcdEventDomain domain);
    ~IpcdEvent() override = default;

    IpcdEventDomain domain() const;

    virtual void dispatch(IpcdServiceManager& serviceManager) = 0;

private:
    IpcdEventDomain m_domain{IpcdEventDomain::Unknown};
};

} // namespace pz::ipcd
