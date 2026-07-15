#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>

namespace pz::apid
{

class ApidServiceManager;

enum class ApidActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
    Ingest = 2
};

class ApidAction : public pz::action::Action
{
public:
    explicit ApidAction(ApidActionDomain domain);
    ~ApidAction() override = default;

    ApidActionDomain domain() const;

    virtual void dispatch(ApidServiceManager& serviceManager) = 0;

private:
    ApidActionDomain m_domain{ApidActionDomain::Unknown};
};

}
