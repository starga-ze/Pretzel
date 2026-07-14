#include "service/web/WebResponseAction.h"

#include <utility>

namespace pz::mgmtd
{

WebResponseAction::WebResponseAction(std::shared_ptr<pz::http::HttpResponder> responder,
                                       pz::http::HttpResponse response)
    : MgmtdAction(MgmtdActionDomain::Http),
      m_responder(std::move(responder)),
      m_response(std::move(response))
{
}

void WebResponseAction::dispatch(MgmtdServiceManager& serviceManager)
{
    (void)serviceManager;  // delivery needs only the responder
    if (m_responder)
        m_responder->send(std::move(m_response));
}

} // namespace pz::mgmtd
