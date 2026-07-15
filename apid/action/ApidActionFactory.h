#pragma once

#include "action/ActionFactory.h"
#include "action/ApidAction.h"

#include <cstdint>
#include <memory>

namespace pz::apid
{

class ApidActionFactory final : public pz::action::ActionFactory<ApidAction, ApidActionDomain>
{
public:
    ApidActionFactory() = default;
    ~ApidActionFactory() override = default;

    std::unique_ptr<ApidAction> create(ApidActionDomain domain, std::uint32_t type) override;
};

}
