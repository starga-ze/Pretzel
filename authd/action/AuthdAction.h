#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::authd
{

class AuthdServiceManager;

enum class AuthdActionDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class AuthdAction : public pz::action::Action
{
public:
    explicit AuthdAction(AuthdActionDomain domain);
    ~AuthdAction() override = default;

    AuthdActionDomain domain() const;

    virtual void dispatch(AuthdServiceManager& serviceManager) = 0;

private:
    AuthdActionDomain m_domain{AuthdActionDomain::Unknown};
};

} // namespace pz::authd
