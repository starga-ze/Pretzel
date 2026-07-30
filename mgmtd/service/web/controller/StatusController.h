#pragma once

namespace pz::mgmtd
{

class WebRouter;

// GET /api/status/devices — the set of currently-active device targets (IPs), as engined marks
// them from ICMP liveness. One domain, one file: the handler is private to the .cpp; only route
// registration is exposed.
class StatusController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
