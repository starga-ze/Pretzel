#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// GET /api/status/devices — every managed device's three-layer health (reachable / credential / api),
// joined from engined's projection and the operator's config. An instance owned by WebService,
// reached from its dispatch switch by member call.
class StatusController
{
public:
    void deviceStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/topology — the SASE fabric plus the on-premise firewalls, in one answer.
    void siteTopology(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
