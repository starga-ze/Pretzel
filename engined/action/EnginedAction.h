#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::engined
{

class EnginedServiceManager;

enum class EnginedActionDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1,
    Heartbeat = 2
};

class EnginedAction : public nf::action::Action
{
public:
    explicit EnginedAction(EnginedActionDomain domain);
    ~EnginedAction() override = default;

    EnginedActionDomain domain() const;

    virtual void dispatch(EnginedServiceManager& serviceManager) = 0;

private:
    EnginedActionDomain m_domain{EnginedActionDomain::Unknown};
};

} // namespace nf::engined
