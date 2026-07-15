#pragma once

#include "action/ActionFactory.h"
#include "action/IpcdAction.h"

#include <cstdint>
#include <memory>

namespace pz::ipcd
{

class IpcdActionFactory final : public pz::action::ActionFactory<IpcdAction, IpcdActionDomain>
{
public:
    IpcdActionFactory() = default;
    ~IpcdActionFactory() override = default;

    std::unique_ptr<IpcdAction> create(IpcdActionDomain domain, std::uint32_t type) override;
};

}
