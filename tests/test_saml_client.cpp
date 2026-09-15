// authd's SAML client, over the half that does not need a signed assertion.
//
// buildAuthnRedirectUrl() has to produce something an IdP can actually read back: the AuthnRequest
// is DEFLATE'd, base64'd and percent-encoded, and every one of those three steps is a place where
// a plausible-looking URL comes out the far end as garbage. So the assertions here undo the whole
// chain and read the XML, rather than pattern-matching the encoded blob.
//
// verifyResponse() is covered only on its rejection paths — the ones that decide whether a forged
// login gets in. Those all run before any valid signature would be needed, which is what makes
// them testable without minting a certificate.

#include "service/auth/SamlClient.h"

#include "util/Logger.h"

#include <zlib.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using pz::authd::SamlClient;

namespace
{

SamlClient::Config baseConfig()
{
    SamlClient::Config cfg;
    cfg.enabled = true;
    cfg.idpSsoUrl = "https://idp.example.com/sso/saml";
    cfg.idpEntityId = "https://idp.example.com/entity";
    cfg.spEntityId = "https://pretzel.local/saml/metadata";
    cfg.acsUrl = "https://pretzel.local/api/auth/saml/acs";
    return cfg;
}

SamlClient configured(const SamlClient::Config& cfg)
{
    SamlClient c;
    c.configure(cfg);
    return c;
}

std::string queryValue(const std::string& url, const std::string& key)
{
    const auto q = url.find('?');
    if (q == std::string::npos)
        return {};

    std::size_t pos = q + 1;
    while (pos <= url.size())
    {
        const auto amp = url.find('&', pos);
        const std::string pair = url.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const auto eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key)
            return pair.substr(eq + 1);
        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return {};
}

bool hasQueryKey(const std::string& url, const std::string& key)
{
    const auto q = url.find('?');
    if (q == std::string::npos)
        return false;
    return url.find("&" + key + "=", q) != std::string::npos || url.compare(q + 1, key.size() + 1, key + "=") == 0;
}

std::string percentDecode(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string base64Decode(const std::string& in)
{
    auto val = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };

    std::string out;
    int buf = 0, bits = 0;
    for (char c : in)
    {
        if (c == '=')
            break;
        const int v = val(c);
        if (v < 0)
            return {};
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// Raw DEFLATE (windowBits -15), the framing RFC 4180 §3.4.4.1 mandates for HTTP-Redirect. If the
// client ever emitted a zlib- or gzip-wrapped stream instead, this call is what would notice.
std::string rawInflate(const std::string& in)
{
    z_stream zs{};
    if (inflateInit2(&zs, -15) != Z_OK)
        return {};

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());

    std::string out;
    char chunk[4096];
    int ret = Z_OK;
    do
    {
        zs.next_out = reinterpret_cast<Bytef*>(chunk);
        zs.avail_out = sizeof(chunk);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END)
            break;
        out.append(chunk, sizeof(chunk) - zs.avail_out);
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return ret == Z_STREAM_END ? out : std::string{};
}

// The full journey back: query value -> percent-decode -> base64-decode -> inflate -> XML.
std::string authnRequestXml(const std::string& redirectUrl)
{
    return rawInflate(base64Decode(percentDecode(queryValue(redirectUrl, "SAMLRequest"))));
}

std::string base64EncodePlain(const std::string& raw)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < raw.size(); i += 3)
    {
        const std::uint32_t n = (static_cast<unsigned char>(raw[i]) << 16) |
                                (static_cast<unsigned char>(raw[i + 1]) << 8) |
                                static_cast<unsigned char>(raw[i + 2]);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }
    if (i < raw.size())
    {
        std::uint32_t n = static_cast<unsigned char>(raw[i]) << 16;
        if (i + 1 < raw.size())
            n |= static_cast<unsigned char>(raw[i + 1]) << 8;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back((i + 1 < raw.size()) ? tbl[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

}

// ── buildAuthnRedirectUrl: refusal ──────────────────────────────────────────────────────────────

TEST(SamlAuthnRequest, RefusesWhenSamlIsDisabled)
{
    auto cfg = baseConfig();
    cfg.enabled = false;

    const auto r = configured(cfg).buildAuthnRedirectUrl("");
    EXPECT_FALSE(r.success);
    EXPECT_EQ("saml disabled", r.error);
    EXPECT_TRUE(r.redirectUrl.empty());
}

// ── buildAuthnRedirectUrl: the URL around the payload ───────────────────────────────────────────

TEST(SamlAuthnRequest, SendsTheBrowserToTheConfiguredSsoUrl)
{
    const auto r = configured(baseConfig()).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(0u, r.redirectUrl.rfind("https://idp.example.com/sso/saml?", 0)) << r.redirectUrl;
}

TEST(SamlAuthnRequest, AppendsToAnSsoUrlThatAlreadyCarriesAQuery)
{
    auto cfg = baseConfig();
    cfg.idpSsoUrl = "https://idp.example.com/sso/saml?tenant=acme";

    const auto r = configured(cfg).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;

    // A second '?' would make the whole SAMLRequest part of the previous parameter's value.
    EXPECT_EQ(1u, std::count(r.redirectUrl.begin(), r.redirectUrl.end(), '?')) << r.redirectUrl;
    EXPECT_EQ("acme", queryValue(r.redirectUrl, "tenant"));
    EXPECT_FALSE(queryValue(r.redirectUrl, "SAMLRequest").empty());
}

TEST(SamlAuthnRequest, PercentEncodesTheBase64PayloadSoItSurvivesTheQueryString)
{
    const auto r = configured(baseConfig()).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;

    const std::string encoded = queryValue(r.redirectUrl, "SAMLRequest");
    ASSERT_FALSE(encoded.empty());

    // base64 emits '+', '/' and '='. Raw in a query string, '+' silently becomes a space at the
    // other end and the payload no longer inflates.
    EXPECT_EQ(std::string::npos, encoded.find('+'));
    EXPECT_EQ(std::string::npos, encoded.find('/'));
    EXPECT_EQ(std::string::npos, encoded.find('='));
}

TEST(SamlAuthnRequest, CarriesRelayStateOnlyWhenThereIsOne)
{
    auto client = configured(baseConfig());

    const auto without = client.buildAuthnRedirectUrl("");
    ASSERT_TRUE(without.success) << without.error;
    EXPECT_FALSE(hasQueryKey(without.redirectUrl, "RelayState")) << without.redirectUrl;

    const auto with = client.buildAuthnRedirectUrl("/monitor/system-log");
    ASSERT_TRUE(with.success) << with.error;
    EXPECT_EQ("%2Fmonitor%2Fsystem-log", queryValue(with.redirectUrl, "RelayState"));
}

TEST(SamlAuthnRequest, RelayStateCannotSmuggleInAnotherParameter)
{
    const auto r = configured(baseConfig()).buildAuthnRedirectUrl("x&SAMLRequest=forged");
    ASSERT_TRUE(r.success) << r.error;

    // Decoding the value must give the caller's string back whole, and the real payload must not
    // have been displaced by the injected one.
    EXPECT_EQ("x&SAMLRequest=forged", percentDecode(queryValue(r.redirectUrl, "RelayState")));
    EXPECT_FALSE(authnRequestXml(r.redirectUrl).empty()) << "the genuine SAMLRequest must still parse";
}

// ── buildAuthnRedirectUrl: what the IdP actually receives ───────────────────────────────────────

TEST(SamlAuthnRequest, PayloadInflatesBackToTheAuthnRequestXml)
{
    const auto r = configured(baseConfig()).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;

    const std::string xml = authnRequestXml(r.redirectUrl);
    ASSERT_FALSE(xml.empty()) << "deflate/base64/percent chain did not round-trip";

    EXPECT_NE(std::string::npos, xml.find("<samlp:AuthnRequest"));
    EXPECT_NE(std::string::npos, xml.find("urn:oasis:names:tc:SAML:2.0:protocol"));
    EXPECT_NE(std::string::npos, xml.find("Version=\"2.0\""));
}

TEST(SamlAuthnRequest, PayloadNamesThisServiceProviderAndItsAcsUrl)
{
    auto cfg = baseConfig();
    cfg.spEntityId = "https://pretzel.local/saml/metadata";
    cfg.acsUrl = "https://pretzel.local/api/auth/saml/acs";

    const auto r = configured(cfg).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;

    const std::string xml = authnRequestXml(r.redirectUrl);
    ASSERT_FALSE(xml.empty());

    // The IdP posts its answer to whatever ACS URL this request names, so a wrong value here sends
    // a valid assertion somewhere else entirely.
    EXPECT_NE(std::string::npos, xml.find("AssertionConsumerServiceURL=\"" + cfg.acsUrl + "\""));
    EXPECT_NE(std::string::npos, xml.find("<saml:Issuer>" + cfg.spEntityId + "</saml:Issuer>"));
    EXPECT_NE(std::string::npos, xml.find("Destination=\"" + cfg.idpSsoUrl + "\""));
}

TEST(SamlAuthnRequest, PayloadIdMatchesTheReturnedRequestId)
{
    const auto r = configured(baseConfig()).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;

    // The caller keeps requestId to match InResponseTo against; it only works if it is the ID that
    // actually went out.
    EXPECT_NE(std::string::npos, authnRequestXml(r.redirectUrl).find("ID=\"" + r.requestId + "\""));
}

TEST(SamlAuthnRequest, RequestIdIsAValidXmlIdAndFreshEveryTime)
{
    auto client = configured(baseConfig());

    const auto first = client.buildAuthnRedirectUrl("");
    const auto second = client.buildAuthnRedirectUrl("");
    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);

    // xsd:ID is an NCName: it may not start with a digit, which is why the leading underscore is
    // there and not decoration.
    ASSERT_FALSE(first.requestId.empty());
    EXPECT_EQ('_', first.requestId.front());
    EXPECT_EQ(33u, first.requestId.size());
    EXPECT_NE(first.requestId, second.requestId);
}

TEST(SamlAuthnRequest, IssueInstantIsUtcInXmlDateTimeForm)
{
    const auto r = configured(baseConfig()).buildAuthnRedirectUrl("");
    ASSERT_TRUE(r.success) << r.error;

    const std::string xml = authnRequestXml(r.redirectUrl);
    const auto at = xml.find("IssueInstant=\"");
    ASSERT_NE(std::string::npos, at);

    // 2026-09-15T04:05:06Z — the trailing Z is what tells the IdP this is UTC rather than local
    // time, and a clock-skew window is judged against it.
    const std::string instant = xml.substr(at + 14, 20);
    ASSERT_EQ(20u, instant.size()) << instant;
    EXPECT_EQ('T', instant[10]) << instant;
    EXPECT_EQ('Z', instant[19]) << instant;
    EXPECT_EQ('-', instant[4]) << instant;
    EXPECT_EQ(':', instant[13]) << instant;
}

// ── verifyResponse: the rejections that keep a forged login out ─────────────────────────────────

TEST(SamlVerifyResponse, RefusesWhenSamlIsDisabled)
{
    auto cfg = baseConfig();
    cfg.enabled = false;

    const auto r = configured(cfg).verifyResponse(base64EncodePlain("<Response/>"));
    EXPECT_FALSE(r.success);
    EXPECT_EQ("saml disabled", r.error);
}

TEST(SamlVerifyResponse, RejectsAnythingThatIsNotBase64)
{
    auto client = configured(baseConfig());

    for (const char* body : {"", "not base64 at all!", "****", "@@@@"})
    {
        const auto r = client.verifyResponse(body);
        EXPECT_FALSE(r.success) << "body '" << body << "'";
        EXPECT_EQ("bad base64 SAMLResponse", r.error) << "body '" << body << "'";
    }
}

TEST(SamlVerifyResponse, RejectsBase64ThatDecodesToSomethingOtherThanXml)
{
    const auto r = configured(baseConfig()).verifyResponse(base64EncodePlain("this is plain text, not xml"));
    EXPECT_FALSE(r.success);
    EXPECT_EQ("xml parse failed", r.error);
}

TEST(SamlVerifyResponse, RejectsWellFormedXmlThatIsNotSigned)
{
    const std::string unsignedResponse =
        R"(<samlp:Response xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol")"
        R"( xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion" ID="_r1" Version="2.0">)"
        R"(<saml:Assertion ID="_a1" Version="2.0">)"
        R"(<saml:Subject><saml:NameID>admin@example.com</saml:NameID></saml:Subject>)"
        R"(</saml:Assertion></samlp:Response>)";

    // This is the whole point of SAML: a response that merely *says* who the user is proves
    // nothing. Without a signature the assertion is attacker-supplied text.
    const auto r = configured(baseConfig()).verifyResponse(base64EncodePlain(unsignedResponse));
    EXPECT_FALSE(r.success) << "an unsigned assertion was accepted";
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.username.empty()) << "no identity may be reported from an unverified response";
}

TEST(SamlVerifyResponse, RejectsAResponseSignedByAnUnknownIssuerCertificate)
{
    // A Signature element that is present but cannot be checked against the configured IdP
    // certificate must fail closed, not fall back to "well, it had a signature".
    const std::string forged =
        R"(<samlp:Response xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol")"
        R"( xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion" ID="_r1" Version="2.0">)"
        R"(<saml:Assertion ID="_a1" Version="2.0">)"
        R"(<ds:Signature xmlns:ds="http://www.w3.org/2000/09/xmldsig#"><ds:SignedInfo/>)"
        R"(<ds:SignatureValue>AAAA</ds:SignatureValue></ds:Signature>)"
        R"(<saml:Subject><saml:NameID>admin@example.com</saml:NameID></saml:Subject>)"
        R"(</saml:Assertion></samlp:Response>)";

    const auto r = configured(baseConfig()).verifyResponse(base64EncodePlain(forged));
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.username.empty());
    EXPECT_TRUE(r.groups.empty());
}

// SamlClient::configure() logs, and LOG_* dereferences the logger without checking it. Initialise
// it into a scratch file and mute it — this suite asserts on return values, not on log lines.
int main(int argc, char** argv)
{
    const auto logFile = std::filesystem::temp_directory_path() / "pretzel-tests" / "saml-client.log";
    pz::util::Logger::Init("test-saml-client", logFile.string(), 256 * 1024, 1);
    pz::util::Logger::GetLogger()->set_level(spdlog::level::off);

    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();

    pz::util::Logger::Shutdown();
    return rc;
}
