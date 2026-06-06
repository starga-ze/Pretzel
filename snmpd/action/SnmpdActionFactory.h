#pragma once

#include "action/ActionFactory.h"
#include "action/SnmpdAction.h"

#include <cstdint>
#include <memory>

namespace nf::snmpd
{

class SnmpdActionFactory final : public nf::action::ActionFactory<SnmpdAction, SnmpdActionDomain>
{
public:
    SnmpdActionFactory() = default;
    ~SnmpdActionFactory() override = default;

    std::unique_ptr<SnmpdAction> create(SnmpdActionDomain domain, std::uint32_t type) override;
};

} // namespace nf::snmpd
