#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace pz::collectord
{

class CollectordServiceManager;

enum class CollectordActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Heartbeat = 2,
    Scan = 3,
};

class CollectordAction : public pz::action::Action
{
public:
    explicit CollectordAction(CollectordActionDomain domain);
    ~CollectordAction() override = default;

    CollectordActionDomain domain() const;

    virtual void dispatch(CollectordServiceManager& serviceManager) = 0;

private:
    CollectordActionDomain m_domain{CollectordActionDomain::Unknown};
};

}
