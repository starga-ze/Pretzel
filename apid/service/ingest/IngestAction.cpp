#include "service/ingest/IngestAction.h"

#include "service/ApidServiceManager.h"

#include <utility>

namespace pz::apid
{

IngestAction::IngestAction(pz::http::HttpResponse response, pz::http::SessionId id)
    : ApidAction(ApidActionDomain::Ingest),
      m_response(std::move(response)),
      m_sessionId(id)
{
}

void IngestAction::dispatch(ApidServiceManager& serviceManager)
{
    serviceManager.ingestService().handleAction(serviceManager, *this);
}

pz::http::HttpResponse& IngestAction::response()
{
    return m_response;
}

pz::http::SessionId IngestAction::sessionId() const
{
    return m_sessionId;
}

} // namespace pz::apid
