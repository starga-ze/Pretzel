#include "api/ApiEngineHandler.h"
#include "api/ApiEngine.h"
#include "api/ApiPacket.h"
#include "router/ScandRxRouter.h"
#include "util/Logger.h"

#include <memory>

namespace pz::scand
{

ApiEngineHandler::ApiEngineHandler(ApiEngine* apiEngine) : m_apiEngine(apiEngine)
{
}

void ApiEngineHandler::egress(std::map<std::string, ApiCredential> devices)
{
    if (!m_apiEngine)
    {
        LOG_ERROR("ApiEngine is not initialized");
        return;
    }

    m_apiEngine->startScan(std::move(devices));
}

void ApiEngineHandler::onScanComplete(std::vector<SnmpDevice> devices)
{
    if (!m_rxRouter)
    {
        LOG_WARN("rxRouter is nullptr, dropping results (count={})", devices.size());
        return;
    }

    m_rxRouter->handleApiPacket(std::make_unique<ApiPacket>(std::move(devices)));
}

void ApiEngineHandler::setRxRouter(ScandRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

}
