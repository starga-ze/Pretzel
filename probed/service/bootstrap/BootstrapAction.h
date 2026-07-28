#pragma once

#include "action/ProbedAction.h"

#include <cstdint>

namespace pz::probed
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public ProbedAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(ProbedServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

}
