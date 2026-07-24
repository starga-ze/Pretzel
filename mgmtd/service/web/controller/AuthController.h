#pragma once

namespace pz::mgmtd
{

class WebRouter;

// Local session auth: POST /api/login, POST /api/logout, POST /api/change-password, GET /api/whoami.
// The SSO/SAML sign-in flow lives in its own domain (SsoController); this is the password path. One
// domain, one file: the handlers and their helpers are private to the .cpp; only route registration
// is exposed.
class AuthController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
