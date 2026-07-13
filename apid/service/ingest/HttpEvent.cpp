#include "service/ingest/HttpEvent.h"

#include "service/ApidServiceManager.h"

namespace pz::apid
{

HttpEvent::HttpEvent(pz::http::HttpRequestView request,
                     std::shared_ptr<pz::http::HttpResponse> response)
    : ApidEvent(ApidEventDomain::Http),
      m_request(std::move(request)),
      m_response(std::move(response))
{
}

void HttpEvent::dispatch(ApidServiceManager& serviceManager)
{
    serviceManager.ingestService().handleEvent(serviceManager, *this);
}

const pz::http::HttpRequestView& HttpEvent::request() const
{
    return m_request;
}

pz::http::HttpResponse& HttpEvent::response() const
{
    return *m_response;
}

} // namespace pz::apid
