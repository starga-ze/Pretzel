#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Local session auth: POST /api/login, POST /api/logout, POST /api/change-password, GET /api/whoami.
// The SSO/SAML sign-in flow lives in its own domain (SsoController); this is the password path. An
// instance owned by WebService, reached from its dispatch switch by member call. Handlers are pure
// request→response glue — the router applies the session and must-change gates before calling them.
class AuthController
{
public:
    void login(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void logout(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void changePassword(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
    void whoami(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
