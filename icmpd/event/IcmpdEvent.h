#pragma once

#include "event/Event.h"

#include <cstdint>

namespace nf::icmpd
{

enum class IcmpdEventDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Probe = 2,
    Scan = 3,
};

class IcmpdEvent : public nf::event::Event
{
public:
    explicit IcmpdEvent(IcmpdEventDomain domain)
        : m_domain(domain)
    {
    }

    ~IcmpdEvent() override = default;

    IcmpdEventDomain domain() const
    {
        return m_domain;
    }

private:
    IcmpdEventDomain m_domain{IcmpdEventDomain::Unknown};
};

} // namespace nf::icmpd
