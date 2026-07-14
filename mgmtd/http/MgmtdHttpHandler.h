#pragma once

#include "http/HttpHandler.h"

namespace pz::mgmtd
{

class MgmtdRxRouter;

// Handler layer: the daemon's plug into the shared pz::http transport. It is trivial now —
// the session already delivers a transport-agnostic HttpRequest and a responder, so
// this just forwards both to the RxRouter (which posts a WebEvent, exactly like IPC
// ingress). The response is produced asynchronously and delivered via the responder.
//
//   HttpSession -> MgmtdHttpHandler -> MgmtdRxRouter::dispatchHttp -> WebEvent -> WebService
class MgmtdHttpHandler : public pz::http::HttpHandler
{
public:
    explicit MgmtdHttpHandler(MgmtdRxRouter* rxRouter);

    void handle(pz::http::HttpRequest request,
                std::shared_ptr<pz::http::HttpResponder> responder) override;

private:
    MgmtdRxRouter* m_rxRouter{nullptr};
};

} // namespace pz::mgmtd
