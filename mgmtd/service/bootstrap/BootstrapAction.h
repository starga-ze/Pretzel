#pragma once

#include "action/MgmtdAction.h"

#include <cstdint>

namespace nf::mgmtd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendClientHello  = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public MgmtdAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(MgmtdServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

} // namespace nf::mgmtd
