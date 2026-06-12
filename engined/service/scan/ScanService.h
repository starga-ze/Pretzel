#pragma once

#include "service/scan/ScanEvent.h"

namespace pz::engined
{

class EnginedServiceManager;

// Control-plane hub relay for the SNMP scan flow. Both directions transit engined:
//   mgmtd → engined → snmpd   (SnmpScanRequest)
//   snmpd → engined → mgmtd   (SnmpResult)
// engined logs each hop for awareness/sequencing but never owns the payload.
class ScanService
{
public:
    ScanService() = default;
    ~ScanService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const ScanEvent& event);
};

} // namespace pz::engined
