#pragma once

#include "action/ScandAction.h"

#include <cstdint>

namespace pz::scand
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public ScandAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(ScandServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

}
