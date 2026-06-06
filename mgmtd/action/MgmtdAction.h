#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

class MgmtdServiceManager;

enum class MgmtdActionDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1
};

class MgmtdAction : public nf::action::Action
{
public:
    explicit MgmtdAction(MgmtdActionDomain domain);
    ~MgmtdAction() override = default;

    MgmtdActionDomain domain() const;

    virtual void dispatch(MgmtdServiceManager& serviceManager) = 0;

private:
    MgmtdActionDomain m_domain{MgmtdActionDomain::Unknown};
};

} // namespace nf::mgmtd
