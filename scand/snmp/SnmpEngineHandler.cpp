#include "snmp/SnmpEngineHandler.h"
#include "snmp/ScandPacket.h"
#include "router/ScandRxRouter.h"
#include "util/Logger.h"

#include <memory>

namespace pz::scand
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
    m_rxRouter->handleSnmpPacket(std::make_unique<ScandPacket>(std::move(devices)));
}

void SnmpEngineHandler::setRxRouter(ScandRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace pz::scand
