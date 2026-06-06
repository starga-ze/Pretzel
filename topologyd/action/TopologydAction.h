#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::topologyd
{

class TopologydServiceManager;

enum class TopologydActionDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class TopologydAction : public nf::action::Action
{
public:
    explicit TopologydAction(TopologydActionDomain domain);
    ~TopologydAction() override = default;

    TopologydActionDomain domain() const;

    virtual void dispatch(TopologydServiceManager& serviceManager) = 0;

private:
    TopologydActionDomain m_domain{TopologydActionDomain::Unknown};
};

} // namespace nf::topologyd
