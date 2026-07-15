#pragma once

#include "action/ActionFactory.h"
#include "action/EnginedAction.h"

#include <cstdint>
#include <memory>

namespace pz::engined
{

class EnginedActionFactory final : public pz::action::ActionFactory<EnginedAction, EnginedActionDomain>
{
public:
    EnginedActionFactory() = default;
    ~EnginedActionFactory() override = default;

    std::unique_ptr<EnginedAction> create(EnginedActionDomain domain, std::uint32_t type) override;
};

}
