#pragma once

#include "action/ApidAction.h"

#include <cstdint>

namespace pz::apid
{

enum class BootstrapActionType : std::uint32_t
{
    Unknown = 0,
    SendClientHello = 1
};

class BootstrapAction final : public ApidAction
{
public:
    explicit BootstrapAction(BootstrapActionType type);

    BootstrapActionType type() const;

    void dispatch(ApidServiceManager& serviceManager) override;

private:
    BootstrapActionType m_type{BootstrapActionType::Unknown};
};

}
