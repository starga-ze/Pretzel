#pragma once

#include "action/ActionFactory.h"
#include "action/IcmpdAction.h"

#include <cstdint>
#include <memory>

namespace pz::icmpd
{

class IcmpdActionFactory final : public pz::action::ActionFactory<IcmpdAction, IcmpdActionDomain>
{
public:
    IcmpdActionFactory() = default;
    ~IcmpdActionFactory() override = default;

    std::unique_ptr<IcmpdAction> create(IcmpdActionDomain domain, std::uint32_t type) override;
};

}
