#pragma once

#include "action/IcmpdAction.h"

#include <cstdint>

namespace nf::icmpd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public IcmpdAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(IcmpdServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

} // namespace nf::icmpd
