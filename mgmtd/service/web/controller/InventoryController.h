#pragma once

namespace pz::mgmtd
{

class WebRouter;

// GET /api/inventory/status — the set of currently-active device targets (IPs), as engined marks
// them from ICMP liveness. One domain, one file: the handler is private to the .cpp; only route
// registration is exposed.
class InventoryController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
