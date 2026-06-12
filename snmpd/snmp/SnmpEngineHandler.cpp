#include "snmp/SnmpEngineHandler.h"
#include "snmp/SnmpdPacket.h"
#include "router/SnmpdRxRouter.h"
#include "util/Logger.h"

#include <memory>

namespace pz::snmpd
{

void SnmpEngineHandler::onScanComplete(std::vector<SnmpDevice> devices)
{
    if (!m_rxRouter)
    {
        LOG_WARN("SnmpEngineHandler: rxRouter is nullptr, dropping {} results",
                 devices.size());
        return;
    }

    // Wrap the decoded sweep into a typed packet before crossing the router boundary
    // (mirrors IcmpEngineHandler building an IcmpPacket).
    m_rxRouter->handleSnmpPacket(std::make_unique<SnmpdPacket>(std::move(devices)));
}

void SnmpEngineHandler::setRxRouter(SnmpdRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace pz::snmpd
