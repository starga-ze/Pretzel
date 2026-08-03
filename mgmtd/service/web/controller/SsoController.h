#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// SSO / SAML sign-in: GET /api/auth/sso/info, GET /api/auth/sso/login, POST /api/auth/saml/acs,
// GET /api/auth/saml/result. mgmtd builds the AuthnRequest and correlates the assertion that authd
// verifies; the browser polls the result route by ticket. An instance owned by WebService, reached
// from its dispatch switch by member call; the SAML helpers stay private to the .cpp.
class SsoController
{
public:
    void info(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void login(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void samlAcs(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void samlResult(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
