#include "service/ingest/IngestEvent.h"

#include "service/ApidServiceManager.h"

namespace pz::apid
{

IngestEvent::IngestEvent(pz::http::HttpRequest request, pz::http::SessionId id)
    : ApidEvent(ApidEventDomain::Ingest), m_request(std::move(request)), m_sessionId(id)
{
}

void IngestEvent::dispatch(ApidServiceManager& serviceManager)
{
    serviceManager.ingestService().handleEvent(serviceManager, *this);
}

const pz::http::HttpRequest& IngestEvent::request() const
{
    return m_request;
}

pz::http::SessionId IngestEvent::sessionId() const
{
    return m_sessionId;
}

}
