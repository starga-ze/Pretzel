#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::probed
{

class ProbedServiceManager;

enum class ProbedActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Icmp = 2,
    Heartbeat = 3
};

class ProbedAction : public pz::action::Action
{
public:
    explicit ProbedAction(ProbedActionDomain domain);
    ~ProbedAction() override = default;

    ProbedActionDomain domain() const;

    virtual void dispatch(ProbedServiceManager& serviceManager) = 0;

private:
    ProbedActionDomain m_domain{ProbedActionDomain::Unknown};
};

}
