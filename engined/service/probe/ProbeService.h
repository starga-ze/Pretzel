#pragma once

#include "service/probe/ProbeEvent.h"

namespace pz::engined
{

class EnginedServiceManager;

// Control-plane hub relay for the probe flow: icmpd sends its ProbeResult to engined
// (not straight to mgmtd), engined logs it for awareness/sequencing, then forwards it
// to mgmtd. Pure relay — engined never persists or interprets the payload.
class ProbeService
{
public:
    ProbeService() = default;
    ~ProbeService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const ProbeEvent& event);
};

} // namespace pz::engined
