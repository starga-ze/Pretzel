#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::mgmtd
{

class MgmtdServiceManager;

enum class MgmtdActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Heartbeat = 2,
    Web = 3
};

class MgmtdAction : public pz::action::Action
{
public:
    explicit MgmtdAction(MgmtdActionDomain domain);
    ~MgmtdAction() override = default;

    MgmtdActionDomain domain() const;

    virtual void dispatch(MgmtdServiceManager& serviceManager) = 0;

private:
    MgmtdActionDomain m_domain{MgmtdActionDomain::Unknown};
};

}
