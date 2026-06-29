#pragma once

#include "snmp/SnmpTypes.h"

#include <string>
#include <vector>

namespace pz::scand
{

class ScandRxRouter;
class SnmpEngine;

// Single ingress/egress hub for the SnmpEngine, mirroring IcmpEngineHandler:
// egress() forwards a scan request down to SnmpEngine::startScan(); ingress
// (onScanComplete) delivers the completed sweep to the RxRouter.
class SnmpEngineHandler final
{
public:
    explicit SnmpEngineHandler(SnmpEngine* snmpEngine);

    void egress(std::vector<std::string> ips, SnmpScanConfig cfg);

    void onScanComplete(std::vector<SnmpDevice> devices);

    void setRxRouter(ScandRxRouter* rxRouter);

private:
    SnmpEngine*     m_snmpEngine{nullptr};
    ScandRxRouter*  m_rxRouter{nullptr};
};

} // namespace pz::scand
