#pragma once

#include "action/EnginedAction.h"

#include <cstdint>

namespace nf::engined
{

enum class EnginedBootstrapActionType : std::uint32_t
{
    Unknown          = 0,
    SendClientHello  = 1,
    SendSyncRequest  = 2,
    SendRuntimeStart = 3
};

class EnginedBootstrapAction final : public EnginedAction
{
public:
    explicit EnginedBootstrapAction(EnginedBootstrapActionType type);

    EnginedBootstrapActionType type() const;

    void dispatch(EnginedServiceManager& serviceManager) override;

private:
    EnginedBootstrapActionType m_type{EnginedBootstrapActionType::Unknown};
};

} // namespace nf::engined
