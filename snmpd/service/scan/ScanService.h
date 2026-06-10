#pragma once

#include "service/scan/ScanEvent.h"
#include "service/scan/ScanAction.h"

namespace pz::snmpd
{

class SnmpdServiceManager;

class ScanService
{
public:
    ScanService() = default;
    ~ScanService() = default;

    void handleEvent(SnmpdServiceManager& sm, const ScanEvent& event);
    void handleAction(SnmpdServiceManager& sm, const ScanAction& action);
};

} // namespace pz::snmpd
