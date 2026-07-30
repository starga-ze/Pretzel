#pragma once

#include "action/ProbedAction.h"

#include <cstdint>

namespace pz::probed
{

class ProbedServiceManager;

enum class IcmpActionType : std::uint32_t
{
    Unknown = 0,
    StartProbe = 1,
    SendProbeBatch = 2,
    SendProbeResult = 3,
};

class IcmpAction final : public ProbedAction
{
public:
    explicit IcmpAction(IcmpActionType type);

    IcmpActionType type() const;

    void dispatch(ProbedServiceManager& serviceManager) override;

private:
    IcmpActionType m_type = IcmpActionType::Unknown;
};

}
