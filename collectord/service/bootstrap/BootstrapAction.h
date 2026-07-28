#pragma once

#include "action/CollectordAction.h"

#include <cstdint>

namespace pz::collectord
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1,
    SendRuntimeReady = 2
};

class BootstrapAction final : public CollectordAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(CollectordServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

}
