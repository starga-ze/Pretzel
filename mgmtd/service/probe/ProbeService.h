#pragma once

#include "service/probe/ProbeEvent.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

class ProbeService
{
public:
    ProbeService() = default;
    ~ProbeService() = default;

    void handleEvent(MgmtdServiceManager& serviceManager,
                     const ProbeEvent& event);
};

} // namespace pz::mgmtd
