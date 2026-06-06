#pragma once

#include "service/probe/ProbeEvent.h"

namespace nf::mgmtd
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

} // namespace nf::mgmtd
