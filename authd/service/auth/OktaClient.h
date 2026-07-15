#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace pz::authd
{

class OktaClient
{
public:
    struct Config
    {
        bool enabled{false};
        std::string issuer;
        std::string clientId;
        std::string clientSecret;
        std::string redirectUri;
        std::string scopes{"openid email profile"};
        bool verifyTls{true};
        int timeoutMs{5000};
        std::uint64_t txnTtlSec{300};
    };

    struct Result
    {
        bool success{false};
        std::string username;
        std::string error;
    };

    struct StartResult
    {
        bool success{false};
        std::string authorizeUrl;
        std::string state;
        std::string error;
    };

    void configure(const Config& cfg);
    bool enabled() const
    {
        return m_cfg.enabled;
    }

    StartResult buildAuthorizeUrl();
    Result exchangeAndVerify(const std::string& code, const std::string& state);

private:
    struct Txn
    {
        std::string nonce;
        std::string codeVerifier;
        std::uint64_t expiresAt{0};
    };

    struct Endpoint
    {
        std::string host;
        std::uint16_t port{443};
        std::string basePath;
    };

    static std::optional<Endpoint> parseIssuer(const std::string& issuer);
    void pruneExpired(std::uint64_t nowSec);

    bool verifyIdToken(const std::string& idToken, const std::string& expectedNonce, std::string& emailOut,
                       std::string& errOut) const;

    bool verifySignatureRs256(const std::string& signingInput, const std::string& signatureB64Url,
                              const std::string& kid, std::string& errOut) const;

private:
    Config m_cfg;
    std::unordered_map<std::string, Txn> m_txns;
};

}
