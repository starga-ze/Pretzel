#pragma once

#include "action/ActionFactory.h"
#include "action/EnginedAction.h"

#include <cstdint>
#include <memory>

namespace nf::engined
{

class EnginedActionFactory final : public nf::action::ActionFactory<EnginedAction, EnginedActionDomain>
{
public:
    EnginedActionFactory() = default;
    ~EnginedActionFactory() override = default;

    std::unique_ptr<EnginedAction> create(EnginedActionDomain domain, std::uint32_t type) override;
};

} // namespace nf::engined
