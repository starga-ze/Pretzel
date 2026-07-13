#pragma once

#include "event/ApidEvent.h"

#include "http/HttpExchange.h"

#include <memory>

namespace pz::apid
{

// An inbound HTTP request, carried through the ServiceManager event queue exactly like an
// IPC-derived event. It holds the transport-agnostic request view and a shared response
// slot the service fills; because ServiceManager::execute() drains the queue synchronously
// within a tick, the handler can read the slot right after dispatching.
class HttpEvent final : public ApidEvent
{
public:
    HttpEvent(pz::http::HttpRequestView request,
              std::shared_ptr<pz::http::HttpResponse> response);

    void dispatch(ApidServiceManager& serviceManager) override;

    const pz::http::HttpRequestView& request() const;
    pz::http::HttpResponse& response() const;

private:
    pz::http::HttpRequestView m_request;
    std::shared_ptr<pz::http::HttpResponse> m_response;
};

} // namespace pz::apid
