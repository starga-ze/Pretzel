#pragma once

#include "action/TopologydAction.h"

#include <cstdint>

namespace pz::topologyd
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown            = 0,
    SendClientHello    = 1,
    SendRuntimeReady   = 2
};

class BootstrapAction final : public TopologydAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    void dispatch(TopologydServiceManager& serviceManager) override;

    BootstrapActionType type() const;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

} // namespace pz::topologyd
