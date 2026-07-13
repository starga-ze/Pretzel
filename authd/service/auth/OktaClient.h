#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace pz::authd
{

// Owns the OIDC (Okta) transaction end-to-end so the client secret and id_token
// verification never leave authd:
//   1) buildAuthorizeUrl()  — mints state/nonce/PKCE, stores the pending txn, returns
//                             the Okta /authorize URL for mgmtd to 302-redirect to.
//   2) exchangeAndVerify()  — on the callback, matches state, swaps the code for tokens
//                             at /token, then validates the id_token (signature+claims).
//
// Authorization Code + PKCE, confidential client (client_secret). All HTTP is done with
// the shared blocking HttpsClient — safe here because authd's loop calls this inline and
// the calls are short; bump the loop tick budget if Okta latency ever matters.
class OktaClient
{
public:
    struct Config
    {
        bool        enabled{false};
        std::string issuer;        // e.g. https://dev-123.okta.com/oauth2/default
        std::string clientId;
        std::string clientSecret;
        std::string redirectUri;   // must exactly match the app's registered callback
        std::string scopes{"openid email profile"};
        bool        verifyTls{true};
        int         timeoutMs{5000};
        std::uint64_t txnTtlSec{300};   // authorize→callback window
    };

    struct Result
    {
        bool        success{false};
        std::string username;   // mapped from the id_token (email claim)
        std::string error;
    };

    struct StartResult
    {
        bool        success{false};
        std::string authorizeUrl;
        std::string state;
        std::string error;
    };

    void configure(const Config& cfg);
    bool enabled() const { return m_cfg.enabled; }

    StartResult buildAuthorizeUrl();
    Result      exchangeAndVerify(const std::string& code, const std::string& state);

private:
    struct Txn
    {
        std::string   nonce;
        std::string   codeVerifier;
        std::uint64_t expiresAt{0};
    };

    // issuer split into host + base path for HttpsClient (which takes host/port/target).
    struct Endpoint
    {
        std::string host;
        std::uint16_t port{443};
        std::string basePath;   // e.g. /oauth2/default
    };

    static std::optional<Endpoint> parseIssuer(const std::string& issuer);
    void pruneExpired(std::uint64_t nowSec);

    // JWT id_token verification. Fails closed on any check.
    bool verifyIdToken(const std::string& idToken,
                       const std::string& expectedNonce,
                       std::string& emailOut,
                       std::string& errOut) const;

    // RS256 signature check against the JWKS key selected by the token's `kid`.
    bool verifySignatureRs256(const std::string& signingInput,
                              const std::string& signatureB64Url,
                              const std::string& kid,
                              std::string& errOut) const;

private:
    Config m_cfg;
    std::unordered_map<std::string, Txn> m_txns;   // keyed by state
};

} // namespace pz::authd
