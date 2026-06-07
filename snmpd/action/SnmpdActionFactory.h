#pragma once

#include "action/ActionFactory.h"
#include "action/SnmpdAction.h"

#include <cstdint>
#include <memory>

namespace pz::snmpd
{

class SnmpdActionFactory final : public pz::action::ActionFactory<SnmpdAction, SnmpdActionDomain>
{
public:
    SnmpdActionFactory() = default;
    ~SnmpdActionFactory() override = default;

    std::unique_ptr<SnmpdAction> create(SnmpdActionDomain domain, std::uint32_t type) override;
};

} // namespace pz::snmpd
