#pragma once

#include "action/ActionFactory.h"
#include "action/CollectordAction.h"

#include <cstdint>
#include <memory>

namespace pz::collectord
{

class CollectordActionFactory final : public pz::action::ActionFactory<CollectordAction, CollectordActionDomain>
{
public:
    CollectordActionFactory() = default;
    ~CollectordActionFactory() override = default;

    std::unique_ptr<CollectordAction> create(CollectordActionDomain domain, std::uint32_t type) override;
};

}
