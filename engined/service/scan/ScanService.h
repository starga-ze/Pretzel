#pragma once

#include "service/scan/ScanEvent.h"

namespace pz::engined
{

class EnginedServiceManager;

// Persists SNMP scan results into the snmp_devices table. engined is the single DB
// writer, so snmpd sends its SnmpResult here (not to mgmtd); mgmtd reads the table
// back for /api/devices. The result is an authoritative snapshot of the scanned IPs:
// each row is upserted by IP, leaving rows for unscanned IPs untouched.
class ScanService
{
public:
    ScanService() = default;
    ~ScanService() = default;

    void handleEvent(EnginedServiceManager& serviceManager, const ScanEvent& event);

private:
    void persistDevices(const std::string& payloadJson);
};

} // namespace pz::engined
