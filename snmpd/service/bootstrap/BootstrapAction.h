#pragma once

#include "action/SnmpdAction.h"

#include <cstdint>

namespace nf::snmpd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendClientHello  = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public SnmpdAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(SnmpdServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

} // namespace nf::snmpd
