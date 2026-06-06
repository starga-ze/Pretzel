#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::snmpd
{

class SnmpdServiceManager;

enum class SnmpdActionDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class SnmpdAction : public nf::action::Action
{
public:
    explicit SnmpdAction(SnmpdActionDomain domain);
    ~SnmpdAction() override = default;

    SnmpdActionDomain domain() const;

    virtual void dispatch(SnmpdServiceManager& serviceManager) = 0;

private:
    SnmpdActionDomain m_domain{SnmpdActionDomain::Unknown};
};

} // namespace nf::snmpd
