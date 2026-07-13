#pragma once

#include "http/HttpHandler.h"

namespace pz::apid
{

class ApidRxRouter;

// Handler layer: the transport-facing adapter that plugs apid into the shared pz::http
// transport (HttpListener/HttpSession) via the HttpHandler contract. It is intentionally
// thin — it only translates beast <-> transport-agnostic DTOs and forwards the request to
// the daemon's RxRouter (which handles both IPC and HTTP ingress).
//
//   HttpSession -> ApidHttpHandler -> ApidRxRouter::dispatchHttp -> IngestService
//     transport      handler             router                      service
class ApidHttpHandler : public pz::http::HttpHandler
{
public:
    explicit ApidHttpHandler(ApidRxRouter* rxRouter);

    Response handle(const Request& req) override;

private:
    ApidRxRouter* m_rxRouter{nullptr};
};

} // namespace pz::apid
