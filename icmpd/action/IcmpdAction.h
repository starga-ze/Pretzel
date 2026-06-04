#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <memory>
#include <cstdint>

namespace nf::icmpd
{

class IcmpdServiceManager;

enum class IcmpdActionDomain : std::uint32_t
{
    Unknown = 0,
    Bootstrap = 1,
};

class IcmpdAction : public nf::action::Action
{
public:
    explicit IcmpdAction(IcmpdActionDomain domain);
    ~IcmpdAction() override = default;

    IcmpdActionDomain domain() const;

    virtual void dispatch(IcmpdServiceManager& serviceManager) = 0;

private:
    IcmpdActionDomain m_domain{IcmpdActionDomain::Unknown};
};

} // namespace nf::icmpd
