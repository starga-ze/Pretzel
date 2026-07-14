#pragma once

#include "http/HttpHandler.h"

namespace pz::apid
{

class ApidRxRouter;

// Handler layer: the daemon's plug into the shared pz::http transport. It is trivial now —
// the session already delivers a transport-agnostic HttpRequest and a responder, so
// this just forwards both to the RxRouter (which posts an IngestEvent, exactly like IPC
// ingress). The response is produced asynchronously and delivered via the responder.
//
//   HttpSession -> ApidHttpHandler -> ApidRxRouter::dispatchHttp -> IngestEvent -> IngestService
class ApidHttpHandler : public pz::http::HttpHandler
{
public:
    explicit ApidHttpHandler(ApidRxRouter* rxRouter);

    void handle(pz::http::HttpRequest request,
                std::shared_ptr<pz::http::HttpResponder> responder) override;

private:
    ApidRxRouter* m_rxRouter{nullptr};
};

} // namespace pz::apid
