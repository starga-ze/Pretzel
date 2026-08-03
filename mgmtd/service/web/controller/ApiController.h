#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// API connector tests: POST /api/connector/keygen-test, POST /api/connector/endpoint-test,
// POST /api/connector/sase-test, GET /api/connector/test-result, GET /api/connector/keys-state.
// mgmtd validates the operator's request, delegates the device exchange to collectord — the daemon
// that will actually poll the connector — and the browser polls the result route by ticket. An
// instance owned by WebService, reached from its dispatch switch; its helpers stay private to the .cpp.
class ApiController
{
public:
    void keygenTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void endpointTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void saseTest(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void saseKeyStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void credentialStore(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void tlsProbe(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void testResult(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void keysState(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
