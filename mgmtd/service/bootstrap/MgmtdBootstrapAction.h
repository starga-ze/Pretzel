#pragma once

#include "action/MgmtdAction.h"

#include <cstdint>

namespace nf::mgmtd
{

enum class MgmtdBootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendClientHello  = 1,
    SendRuntimeReady = 2
};

class MgmtdBootstrapAction final : public MgmtdAction
{
public:
    explicit MgmtdBootstrapAction(MgmtdBootstrapActionType type);

    MgmtdBootstrapActionType type() const;

    void dispatch(MgmtdServiceManager& serviceManager) override;

private:
    MgmtdBootstrapActionType m_type{MgmtdBootstrapActionType::Unknown};
};

} // namespace nf::mgmtd
