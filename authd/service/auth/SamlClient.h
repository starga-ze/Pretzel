#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pz::authd
{

// SAML 2.0 SP for Okta. SP-initiated: buildAuthnRedirectUrl() produces the HTTP-Redirect
// URL to Okta; verifyResponse() validates the SAMLResponse POSTed back to the ACS —
// XML-DSig signature (xmlsec, against the IdP cert), then Conditions/Issuer/audience and
// the admin group allowlist.
//
// SECURITY: SAML verification is attack-prone (signature wrapping / XSW). This validates
// the *signed* assertion and reads attributes only from that same node. Review + test
// against the real tenant and XSW vectors before trusting in production.
class SamlClient
{
public:
    struct Config
    {
        bool        enabled{false};
        std::string idpSsoUrl;      // Okta "Identity Provider Single Sign-On URL"
        std::string idpEntityId;    // Okta "Identity Provider Issuer"
        std::string idpCertPem;     // IdP signing cert (PEM); may be loaded from a file path
        std::string spEntityId;     // our SP entityID == SAML Audience
        std::string acsUrl;         // where Okta POSTs the SAMLResponse
        std::string adminGroup;     // required group for admin access
        std::string groupsAttr{"groups"};
        std::string emailAttr{"email"};   // empty → use the Subject NameID
        std::uint64_t clockSkewSec{120};
    };

    struct StartResult
    {
        bool        success{false};
        std::string redirectUrl;
        std::string requestId;   // caller may persist for InResponseTo checking
        std::string error;
    };

    struct Result
    {
        bool                     success{false};
        std::string              username;
        std::vector<std::string> groups;
        std::string              error;
    };

    void configure(const Config& cfg);
    bool enabled() const { return m_cfg.enabled; }

    StartResult buildAuthnRedirectUrl(const std::string& relayState);
    Result      verifyResponse(const std::string& base64SamlResponse);

private:
    static bool ensureXmlSecInit();

    // Verifies the enveloped XML-DSig signature over the assertion using the trusted IdP
    // cert. On success, `signedAssertion` points at the assertion node whose signature was
    // verified (attributes must be read only from there).
    bool verifySignature(void* doc, void*& signedAssertion, std::string& errOut) const;

private:
    Config m_cfg;
};

} // namespace pz::authd
