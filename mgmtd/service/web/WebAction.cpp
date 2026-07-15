#include "service/web/WebAction.h"

#include "service/MgmtdServiceManager.h"

#include <utility>

namespace pz::mgmtd
{

WebAction::WebAction(pz::http::HttpResponse response, pz::http::SessionId id)
    : MgmtdAction(MgmtdActionDomain::Web), m_response(std::move(response)), m_sessionId(id)
{
}

void WebAction::dispatch(MgmtdServiceManager& serviceManager)
{
    serviceManager.webService().handleAction(serviceManager, *this);
}

pz::http::HttpResponse& WebAction::response()
{
    return m_response;
}

pz::http::SessionId WebAction::sessionId() const
{
    return m_sessionId;
}

}
