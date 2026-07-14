#include "service/ingest/IngestResponseAction.h"

#include <utility>

namespace pz::apid
{

IngestResponseAction::IngestResponseAction(std::shared_ptr<pz::http::HttpResponder> responder,
                                       pz::http::HttpResponse response)
    : ApidAction(ApidActionDomain::Http),
      m_responder(std::move(responder)),
      m_response(std::move(response))
{
}

void IngestResponseAction::dispatch(ApidServiceManager& serviceManager)
{
    (void)serviceManager;  // delivery needs only the responder
    if (m_responder)
        m_responder->send(std::move(m_response));
}

} // namespace pz::apid
