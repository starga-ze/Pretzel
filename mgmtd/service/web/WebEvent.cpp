#include "service/web/WebEvent.h"

#include "service/web/WebService.h"
#include "service/MgmtdServiceManager.h"

namespace pz::mgmtd
{

WebEvent::WebEvent(pz::http::HttpRequest request,
                     std::shared_ptr<pz::http::HttpResponder> responder)
    : MgmtdEvent(MgmtdEventDomain::Http),
      m_request(std::move(request)),
      m_responder(std::move(responder))
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

const std::shared_ptr<pz::http::HttpResponder>& WebEvent::responder() const
{
    return m_responder;
}

} // namespace pz::mgmtd
