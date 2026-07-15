#pragma once

#include "action/MgmtdAction.h"

#include <cstdint>

namespace pz::mgmtd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1
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

}
