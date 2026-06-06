#pragma once

#include "action/AuthdAction.h"

#include <cstdint>

namespace nf::authd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendClientHello  = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public AuthdAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(AuthdServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

} // namespace nf::authd
