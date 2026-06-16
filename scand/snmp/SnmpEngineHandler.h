#pragma once

#include "snmp/SnmpTypes.h"

#include <vector>

namespace pz::scand
{

class ScandRxRouter;

// Handles the ingress path only: delivers completed scan results to the
// RxRouter.  Engine control (startScan) is called on SnmpEngine directly.
class SnmpEngineHandler final
{
public:
    void onScanComplete(std::vector<SnmpDevice> devices);

    void setRxRouter(ScandRxRouter* rxRouter);

private:
    ScandRxRouter* m_rxRouter{nullptr};
};

} // namespace pz::scand
