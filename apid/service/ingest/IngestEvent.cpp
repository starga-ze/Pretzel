#include "service/ingest/IngestEvent.h"

#include "service/ApidServiceManager.h"

namespace pz::apid
{

IngestEvent::IngestEvent(pz::http::HttpRequest request,
                     std::shared_ptr<pz::http::HttpResponder> responder)
    : ApidEvent(ApidEventDomain::Http),
      m_request(std::move(request)),
      m_responder(std::move(responder))
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

const std::shared_ptr<pz::http::HttpResponder>& IngestEvent::responder() const
{
    return m_responder;
}

} // namespace pz::apid
