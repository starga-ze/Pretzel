#pragma once

namespace pz::mgmtd
{

class WebRouter;

// SSO / SAML sign-in: GET /api/auth/sso/info, GET /api/auth/sso/login, POST /api/auth/saml/acs,
// GET /api/auth/saml/result. mgmtd builds the AuthnRequest and correlates the assertion that authd
// verifies; the browser polls the result route by ticket. One domain, one file: the handlers and
// their SAML helpers are private to the .cpp; only route registration is exposed.
class SsoController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
