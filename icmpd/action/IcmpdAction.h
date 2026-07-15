#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::icmpd
{

class IcmpdServiceManager;

enum class IcmpdActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Probe = 2,
    Heartbeat = 3
};

class IcmpdAction : public pz::action::Action
{
public:
    explicit IcmpdAction(IcmpdActionDomain domain);
    ~IcmpdAction() override = default;

    IcmpdActionDomain domain() const;

    virtual void dispatch(IcmpdServiceManager& serviceManager) = 0;

private:
    IcmpdActionDomain m_domain{IcmpdActionDomain::Unknown};
};

}
