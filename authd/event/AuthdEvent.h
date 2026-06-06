#pragma once

#include "event/Event.h"

#include <cstdint>
#include <memory>

namespace nf::authd
{

class AuthdServiceManager;

enum class AuthdEventDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class AuthdEvent : public nf::event::Event
{
public:
    explicit AuthdEvent(AuthdEventDomain domain);
    ~AuthdEvent() override = default;

    AuthdEventDomain domain() const;

    virtual void dispatch(AuthdServiceManager& serviceManager) = 0;

private:
    AuthdEventDomain m_domain{AuthdEventDomain::Unknown};
};

} // namespace nf::authd
