#pragma once

#include "action/ActionFactory.h"
#include "action/TopologydAction.h"

#include <memory>

namespace pz::topologyd
{

class TopologydActionFactory : public pz::action::ActionFactory<TopologydAction, TopologydActionDomain>
{
public:
    std::unique_ptr<TopologydAction> create(TopologydActionDomain domain,
                                            std::uint32_t type) override;
};

} // namespace pz::topologyd
