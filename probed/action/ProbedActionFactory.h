#pragma once

#include "action/ActionFactory.h"
#include "action/ProbedAction.h"

#include <cstdint>
#include <memory>

namespace pz::probed
{

class ProbedActionFactory final : public pz::action::ActionFactory<ProbedAction, ProbedActionDomain>
{
public:
    ProbedActionFactory() = default;
    ~ProbedActionFactory() override = default;

    std::unique_ptr<ProbedAction> create(ProbedActionDomain domain, std::uint32_t type) override;
};

}
