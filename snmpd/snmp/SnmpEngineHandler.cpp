#include "snmp/SnmpEngineHandler.h"
#include "router/SnmpdRxRouter.h"
#include "util/Logger.h"

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

    m_rxRouter->handleSnmpScanComplete(std::move(devices));
}

void SnmpEngineHandler::setRxRouter(SnmpdRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace pz::snmpd
