#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace pz::mgmtd
{

class MgmtdServiceManager;

enum class MgmtdEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Http      = 2,
    Heartbeat = 3
};

class MgmtdEvent : public pz::event::Event
{
public:
    explicit MgmtdEvent(MgmtdEventDomain domain);
    ~MgmtdEvent() override = default;

    MgmtdEventDomain domain() const;

    virtual void dispatch(MgmtdServiceManager& serviceManager) = 0;

private:
    MgmtdEventDomain m_domain{MgmtdEventDomain::Unknown};
};

} // namespace pz::mgmtd
