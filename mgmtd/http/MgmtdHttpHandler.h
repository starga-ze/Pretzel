#pragma once

#include "http/HttpHandler.h"

namespace pz::mgmtd
{

class MgmtdHttpRxRouter;

// Handler layer: the transport-facing adapter that plugs mgmtd into the shared pz::http
// transport (HttpListener/HttpSession) via the HttpHandler contract. It is intentionally
// thin — it owns no routing or business logic, it just forwards each parsed request to
// the HTTP router (the dispatch layer) and returns the synchronous response.
//
//   HttpSession -> MgmtdHttpHandler -> MgmtdHttpRxRouter::dispatch -> services
//     transport      handler              router                       service
class MgmtdHttpHandler : public pz::http::HttpHandler
{
public:
    explicit MgmtdHttpHandler(MgmtdHttpRxRouter* rxRouter);

    Response handle(const Request& req) override;

private:
    MgmtdHttpRxRouter* m_rxRouter{nullptr};
};

} // namespace pz::mgmtd
