#pragma once

#include "action/ActionFactory.h"
#include "action/AuthdAction.h"

#include <cstdint>
#include <memory>

namespace nf::authd
{

class AuthdActionFactory final : public nf::action::ActionFactory<AuthdAction, AuthdActionDomain>
{
public:
    AuthdActionFactory() = default;
    ~AuthdActionFactory() override = default;

    std::unique_ptr<AuthdAction> create(AuthdActionDomain domain, std::uint32_t type) override;
};

} // namespace nf::authd
