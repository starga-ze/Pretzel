#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace nf::ipcd
{

class IpcdServiceManager;

enum class IpcdEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1
};

class IpcdEvent : public nf::event::Event
{
public:
    explicit IpcdEvent(IpcdEventDomain domain);
    ~IpcdEvent() override = default;

    IpcdEventDomain domain() const;

    virtual void dispatch(IpcdServiceManager& serviceManager) = 0;

private:
    IpcdEventDomain m_domain{IpcdEventDomain::Unknown};
};

} // namespace nf::ipcd
