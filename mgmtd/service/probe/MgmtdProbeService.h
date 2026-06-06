#pragma once

#include "service/probe/MgmtdProbeEvent.h"

namespace nf::mgmtd
{

class MgmtdServiceManager;

class MgmtdProbeService
{
public:
    MgmtdProbeService() = default;
    ~MgmtdProbeService() = default;

    void handleEvent(MgmtdServiceManager& serviceManager,
                     const MgmtdProbeEvent& event);
};

} // namespace nf::mgmtd
