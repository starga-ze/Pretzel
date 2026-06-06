#pragma once

#include "action/ActionFactory.h"
#include "action/MgmtdAction.h"

#include <cstdint>
#include <memory>

namespace nf::mgmtd
{

class MgmtdActionFactory final : public nf::action::ActionFactory<MgmtdAction, MgmtdActionDomain>
{
public:
    MgmtdActionFactory() = default;
    ~MgmtdActionFactory() override = default;

    std::unique_ptr<MgmtdAction> create(MgmtdActionDomain domain, std::uint32_t type) override;
};

} // namespace nf::mgmtd
