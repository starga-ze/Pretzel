#pragma once

#include "service/scan/ScanAction.h"
#include "service/scan/ScanEvent.h"

#include <vector>

namespace pz::scand
{

class ScandServiceManager;

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

}
