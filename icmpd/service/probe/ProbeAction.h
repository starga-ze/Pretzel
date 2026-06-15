#pragma once

#include "action/IcmpdAction.h"

#include <cstdint>

namespace pz::icmpd
{

class IcmpdServiceManager;

enum class ProbeActionType : std::uint32_t
{
    Unknown          = 0,
    StartProbe       = 1,
    SendProbeBatch   = 2,
    SendProbeResult  = 3,  // send probe results to engined after probe completes
};

class ProbeAction final : public IcmpdAction
{
public:
    explicit ProbeAction(ProbeActionType type);

    ProbeActionType type() const;

    void dispatch(IcmpdServiceManager& serviceManager) override;

private:
    ProbeActionType m_type = ProbeActionType::Unknown;
};

} // namespace pz::icmpd
