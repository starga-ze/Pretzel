#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// GET /api/logs[?daemon=&level=&q=&before=&limit=] — reads the system_log table (engined tails the
// daemon log files into it) with server-side filtering and keyset pagination. An instance owned by
// WebService, reached from its dispatch switch by member call; its query helpers stay private to the .cpp.
class LogsController
{
public:
    void list(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
