#pragma once

#include "event/MgmtdEvent.h"

#include "http/HttpMessage.h"
#include "http/HttpResponder.h"

#include <memory>

namespace pz::mgmtd
{

// An inbound HTTP request carried through the ServiceManager event queue exactly like an
// IPC-derived event. It holds the transport-agnostic request and the responder (the parked
// connection). WebService produces a response and posts a WebResponseAction that delivers
// it via the responder — so the whole cycle is async and symmetric with IPC.
class WebEvent final : public MgmtdEvent
{
public:
    WebEvent(pz::http::HttpRequest request,
              std::shared_ptr<pz::http::HttpResponder> responder);

    void dispatch(MgmtdServiceManager& serviceManager) override;

    const pz::http::HttpRequest&               request() const;
    const std::shared_ptr<pz::http::HttpResponder>& responder() const;

private:
    pz::http::HttpRequest m_request;
    std::shared_ptr<pz::http::HttpResponder> m_responder;
};

} // namespace pz::mgmtd
