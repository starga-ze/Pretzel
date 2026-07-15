#include "service/web/WebEvent.h"

#include "service/MgmtdServiceManager.h"
#include "service/web/WebService.h"

namespace pz::mgmtd
{

WebEvent::WebEvent(pz::http::HttpRequest request, pz::http::SessionId id)
    : MgmtdEvent(MgmtdEventDomain::Web), m_request(std::move(request)), m_sessionId(id)
{
}

void WebEvent::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.webService().handleEvent(serviceManager, *this);
}

const pz::http::HttpRequest& WebEvent::request() const
{
    return m_request;
}

pz::http::SessionId WebEvent::sessionId() const
{
    return m_sessionId;
}

}
