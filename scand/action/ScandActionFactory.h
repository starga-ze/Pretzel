#pragma once

#include "action/ActionFactory.h"
#include "action/ScandAction.h"

#include <cstdint>
#include <memory>

namespace pz::scand
{

class ScandActionFactory final : public pz::action::ActionFactory<ScandAction, ScandActionDomain>
{
public:
    ScandActionFactory() = default;
    ~ScandActionFactory() override = default;

    std::unique_ptr<ScandAction> create(ScandActionDomain domain, std::uint32_t type) override;
};

}
