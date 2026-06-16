#pragma once

#include "action/EnginedAction.h"

#include <cstdint>

namespace pz::engined
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendClientHello  = 1,
    SendSyncRequest  = 2,
    SendRuntimeStart = 3,
    SendReloadFailed = 4   // notify mgmtd + commit queue that the reload did not converge
};

class BootstrapAction final : public EnginedAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(EnginedServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

} // namespace pz::engined
