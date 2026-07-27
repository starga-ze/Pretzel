#pragma once

namespace pz::mgmtd
{

class WebRouter;

// GET /api/logs[?daemon=&level=&q=&before=&limit=] — reads the system_log table (engined tails the
// daemon log files into it) with server-side filtering and keyset pagination. One domain, one file:
// the handler and its helpers are private to the .cpp; only route registration is exposed.
class LogsController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
