#pragma once

#include "action/ActionFactory.h"
#include "action/IpcdAction.h"

#include <cstdint>
#include <memory>

namespace nf::ipcd
{

class IpcdActionFactory final : public nf::action::ActionFactory<IpcdAction, IpcdActionDomain>
{
public:
    IpcdActionFactory() = default;
    ~IpcdActionFactory() override = default;

    std::unique_ptr<IpcdAction> create(IpcdActionDomain domain, std::uint32_t type) override;
};

} // namespace nf::ipcd
