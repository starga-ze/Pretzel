#pragma once

#include "action/Action.h"
#include "ipc/IpcMessage.h"

#include <cstdint>
#include <memory>

namespace nf::ipcd
{

class IpcdServiceManager;

enum class IpcdActionDomain : std::uint32_t
{
    Unknown   = 0,
    Bootstrap = 1
};

class IpcdAction : public nf::action::Action
{
public:
    explicit IpcdAction(IpcdActionDomain domain);
    ~IpcdAction() override = default;

    IpcdActionDomain domain() const;

    virtual void dispatch(IpcdServiceManager& serviceManager) = 0;

private:
    IpcdActionDomain m_domain{IpcdActionDomain::Unknown};
};

} // namespace nf::ipcd
