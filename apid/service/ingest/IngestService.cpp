#include "service/ingest/IngestService.h"

#include "service/ingest/IngestRouting.h"

#include "service/ApidServiceManager.h"
#include "service/ingest/IngestAction.h"
#include "service/ingest/IngestEvent.h"

#include "router/ApidTxRouter.h"

#include "util/Logger.h"

#include <memory>

#include <cstdlib>

namespace pz::apid
{

namespace
{

std::string ingestTokenFromEnv()
{
    const char* v = std::getenv("PZ_APID_TOKEN");
    return (v && *v) ? std::string(v) : std::string("changeme-dev-token");
}

}

IngestService::IngestService() : m_ingestToken(ingestTokenFromEnv())
{
}

void IngestService::handleEvent(ApidServiceManager& serviceManager, const IngestEvent& event)
{
    pz::http::HttpResponse resp;
    route(event.request(), resp);

    serviceManager.postAction(std::make_unique<IngestAction>(std::move(resp), event.sessionId()));
}

void IngestService::handleAction(ApidServiceManager& serviceManager, IngestAction& action)
{
    serviceManager.txRouter().handleHttpMessage(std::move(action.response()), action.sessionId());
}

void IngestService::route(const pz::http::HttpRequest& req, pz::http::HttpResponse& resp)
{
    routeIngest(req, m_ingestToken, resp);
}

}
