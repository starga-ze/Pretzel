#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::scand
{

class ScandServiceManager;

enum class ScandActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Heartbeat = 2,
    Scan = 3,
};

class ScandAction : public pz::action::Action
{
public:
    explicit ScandAction(ScandActionDomain domain);
    ~ScandAction() override = default;

    ScandActionDomain domain() const;

    virtual void dispatch(ScandServiceManager& serviceManager) = 0;

private:
    ScandActionDomain m_domain{ScandActionDomain::Unknown};
};

}
