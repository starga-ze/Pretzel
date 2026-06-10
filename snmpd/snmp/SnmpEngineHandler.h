#pragma once

#include "snmp/SnmpTypes.h"

#include <vector>

namespace pz::snmpd
{

class SnmpdRxRouter;

// Handles the ingress path only: delivers completed scan results to the
// RxRouter.  Engine control (startScan) is called on SnmpEngine directly.
class SnmpEngineHandler final
{
public:
    void onScanComplete(std::vector<SnmpDevice> devices);

    void setRxRouter(SnmpdRxRouter* rxRouter);

private:
    SnmpdRxRouter* m_rxRouter{nullptr};
};

} // namespace pz::snmpd
