#pragma once

namespace pz::mgmtd
{

class WebRouter;

// API connector tests: POST /api/connector/keygen-test, POST /api/connector/endpoint-test,
// GET /api/connector/test-result. mgmtd validates the operator's request, delegates the device
// exchange to scand — the daemon that will actually poll the connector — and the browser polls the
// result route by ticket. One domain, one file: the handlers and their helpers are private to the
// .cpp; only route registration is exposed.
class ConnectorController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
