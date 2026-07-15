#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pz::authd
{

class SamlClient
{
public:
    struct Config
    {
        bool enabled{false};
        std::string idpSsoUrl;
        std::string idpEntityId;
        std::string idpCertPem;
        std::string spEntityId;
        std::string acsUrl;
        std::string adminGroup;
        std::string groupsAttr{"groups"};
        std::string emailAttr{"email"};
        std::uint64_t clockSkewSec{120};
    };

    struct StartResult
    {
        bool success{false};
        std::string redirectUrl;
        std::string requestId;
        std::string error;
    };

    struct Result
    {
        bool success{false};
        std::string username;
        std::vector<std::string> groups;
        std::string error;
    };

    void configure(const Config& cfg);
    bool enabled() const
    {
        return m_cfg.enabled;
    }

    StartResult buildAuthnRedirectUrl(const std::string& relayState);
    Result verifyResponse(const std::string& base64SamlResponse);

private:
    static bool ensureXmlSecInit();

    bool verifySignature(void* doc, void*& signedAssertion, std::string& errOut) const;

private:
    Config m_cfg;
};

}
