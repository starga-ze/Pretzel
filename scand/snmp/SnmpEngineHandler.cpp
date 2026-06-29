#include "snmp/SnmpEngineHandler.h"
#include "snmp/SnmpEngine.h"
#include "snmp/SnmpPacket.h"
#include "router/ScandRxRouter.h"
#include "util/Logger.h"

#include <memory>

namespace pz::scand
{

SnmpEngineHandler::SnmpEngineHandler(SnmpEngine* snmpEngine)
    : m_snmpEngine(snmpEngine)
{
}

void SnmpEngineHandler::egress(std::vector<std::string> ips, SnmpScanConfig cfg)
{
    if (!m_snmpEngine)
    {
        LOG_ERROR("SnmpEngine is not initialized");
        return;
    }

    m_snmpEngine->startScan(std::move(ips), std::move(cfg));
}

void SnmpEngineHandler::onScanComplete(std::vector<SnmpDevice> devices)
{
    if (!m_rxRouter)
    {
        LOG_WARN("rxRouter is nullptr, dropping results (count={})",
                 devices.size());
        return;
    }

    // Wrap the decoded sweep into a typed packet before crossing the router boundary
    // (mirrors IcmpEngineHandler building an IcmpPacket).
    m_rxRouter->handleSnmpPacket(std::make_unique<SnmpPacket>(std::move(devices)));
}

void SnmpEngineHandler::setRxRouter(ScandRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace pz::scand
