#pragma once

#include "service/scan/ScanEvent.h"
#include "service/scan/ScanAction.h"

#include <vector>

namespace pz::scand
{

class ScandServiceManager;

// Fans a ScanRequest out to two independent engines — SnmpEngine (v2c/v3 scan
// methods) and ApiEngine (api scan method) — and merges their completions into
// one ScanResult before replying to engined. engined's ScanResult handling is a
// wholesale replace-then-reapply over probe_devices (see
// engined/service/scan/ScanService::persistDevices), so it must see exactly one
// ScanResult per request even though the two engines complete independently and
// asynchronously — the merge below is what makes that safe.
class ScanService
{
public:
    ScanService() = default;
    ~ScanService() = default;

    void handleEvent(ScandServiceManager& sm, const ScanEvent& event);
    void handleAction(ScandServiceManager& sm, const ScanAction& action);

private:
    void handleScanRequest(ScandServiceManager& sm, const ScanEvent& event);
    void handleSnmpComplete(ScandServiceManager& sm, const std::vector<SnmpDevice>& devices);
    void handleApiComplete(ScandServiceManager& sm, const std::vector<SnmpDevice>& devices);
    void finalizeIfReady(ScandServiceManager& sm);

    bool m_snmpPending{false};
    bool m_apiPending{false};
    std::vector<SnmpDevice> m_collected;
};

} // namespace pz::scand
