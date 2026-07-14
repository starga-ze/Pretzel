#pragma once

#include "event/ApidEvent.h"

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::apid
{

// An inbound HTTP request carried through the ServiceManager event queue exactly like an
// IPC-derived event. It holds the transport-agnostic request and the responder (the parked
// connection). The service produces a response and posts an IngestResponseAction that delivers
// it via the responder — so the whole cycle is async and symmetric with IPC.
class IngestEvent final : public ApidEvent
{
public:
    IngestEvent(pz::http::HttpRequest request,
              std::shared_ptr<pz::http::HttpResponder> responder);

    void dispatch(ApidServiceManager& serviceManager) override;

    const pz::http::HttpRequest&               request() const;
    const std::shared_ptr<pz::http::HttpResponder>& responder() const;

private:
    pz::http::HttpRequest m_request;
    std::shared_ptr<pz::http::HttpResponder> m_responder;
};

} // namespace pz::apid
