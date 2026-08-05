#pragma once

#include "http/HttpMessage.h"

#include <string>

namespace pz::ipc
{
class IpcMessage;
}

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Insight ▸ Infrastructure, both directions of it.
//
// One controller rather than a route on StatusController and a branch in the router, because the two
// halves are one conversation: the browser asks for a site, mgmtd asks topologyd, topologyd answers,
// and the next browser poll serves that answer. Splitting the ask from the answer is what let the
// answer's handling drift into MgmtdRxRouter and grow a JSON parse there.
//
// mgmtd owns no topology logic either way. It asks, it files what came back, it serves it.
class TopologyController
{
public:
    // GET /api/topology?site=<oid> — the SASE fabric plus the on-premise firewalls, in one answer.
    //
    // The HTTP response is never held open for the composition. Responses here are built
    // synchronously on the loop every other daemon's messages arrive on, so waiting for one browser
    // would stall every other daemon's traffic behind it. Instead the answer carries `pending`: "a
    // fresher composition is on its way". The page re-asks in a moment and draws that.
    void siteTopology(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // topologyd answered. The model names the site it was built for, so the answer files itself —
    // no request/response correlation table for something that is simply the latest truth about a
    // named site.
    void onTopologyResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);
};

}
