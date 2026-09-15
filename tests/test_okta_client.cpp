// authd's OIDC client, over the half that never touches the network.
//
// buildAuthorizeUrl() is where the browser is sent, so every security parameter of the flow is
// decided here and nowhere else: the PKCE challenge, the single-use state, the nonce. A mistake in
// this string is not a failure — the login still works — it is a downgrade nobody notices.
//
// exchangeAndVerify() is exercised only up to the point where it would call the IdP; the guards
// before that (disabled, unknown state, replayed state) are what stop a forged callback, and they
// all run before a single byte leaves the process.

#include "service/auth/OktaClient.h"

#include "util/Logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>

using pz::authd::OktaClient;

namespace
{

OktaClient::Config baseConfig()
{
    OktaClient::Config cfg;
    cfg.enabled = true;
    cfg.issuer = "https://dev-123.okta.com/oauth2/default";
    cfg.clientId = "0oa1b2c3d4";
    cfg.clientSecret = "s3cr3t";
    cfg.redirectUri = "https://pretzel.local/api/auth/oidc/callback";
    cfg.scopes = "openid email profile";
    return cfg;
}

OktaClient configured(const OktaClient::Config& cfg)
{
    OktaClient c;
    c.configure(cfg);
    return c;
}

// Split "https://host/path?a=1&b=2" into its query parameters, leaving the values percent-encoded
// so the encoding itself stays visible to the assertions.
std::map<std::string, std::string> queryOf(const std::string& url)
{
    std::map<std::string, std::string> out;
    const auto q = url.find('?');
    if (q == std::string::npos)
        return out;

    std::size_t pos = q + 1;
    while (pos <= url.size())
    {
        const auto amp = url.find('&', pos);
        const std::string pair = url.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const auto eq = pair.find('=');
        if (eq != std::string::npos)
            out[pair.substr(0, eq)] = pair.substr(eq + 1);
        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return out;
}

std::string originOf(const std::string& url)
{
    const auto q = url.find('?');
    return q == std::string::npos ? url : url.substr(0, q);
}

bool isLowerHex(const std::string& s)
{
    for (char c : s)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return !s.empty();
}

// An IdP that cannot be reached and cannot be resolved either: port 1 on the loopback refuses
// immediately, so a test that walks into the HTTP call fails in microseconds instead of waiting
// out a timeout, and never depends on the machine having a network.
OktaClient::Config unreachableIdp()
{
    auto cfg = baseConfig();
    cfg.issuer = "https://127.0.0.1:1/oauth2/default";
    cfg.timeoutMs = 50;
    return cfg;
}

}

// ── buildAuthorizeUrl: refusals ─────────────────────────────────────────────────────────────────

TEST(OktaAuthorizeUrl, RefusesWhenOidcIsDisabled)
{
    auto cfg = baseConfig();
    cfg.enabled = false;

    const auto r = configured(cfg).buildAuthorizeUrl();
    EXPECT_FALSE(r.success);
    EXPECT_EQ("oidc disabled", r.error);
    EXPECT_TRUE(r.authorizeUrl.empty());
}

TEST(OktaAuthorizeUrl, RefusesAnIssuerThatIsNotHttps)
{
    for (const char* issuer : {"http://dev-123.okta.com", "dev-123.okta.com", "HTTPS://dev-123.okta.com",
                               "ftp://dev-123.okta.com", ""})
    {
        auto cfg = baseConfig();
        cfg.issuer = issuer;

        const auto r = configured(cfg).buildAuthorizeUrl();
        EXPECT_FALSE(r.success) << "issuer '" << issuer << "'";
        EXPECT_EQ("bad issuer", r.error) << "issuer '" << issuer << "'";
    }
}

TEST(OktaAuthorizeUrl, RefusesAnIssuerWithNoHost)
{
    auto cfg = baseConfig();
    cfg.issuer = "https:///oauth2/default";

    const auto r = configured(cfg).buildAuthorizeUrl();
    EXPECT_FALSE(r.success);
    EXPECT_EQ("bad issuer", r.error);
}

// ── buildAuthorizeUrl: the parameters that make the flow safe ───────────────────────────────────

TEST(OktaAuthorizeUrl, CarriesEveryParameterTheFlowDependsOn)
{
    const auto r = configured(baseConfig()).buildAuthorizeUrl();
    ASSERT_TRUE(r.success) << r.error;

    const auto q = queryOf(r.authorizeUrl);
    for (const char* key : {"client_id", "response_type", "scope", "redirect_uri", "state", "nonce", "code_challenge",
                            "code_challenge_method"})
    {
        EXPECT_EQ(1u, q.count(key)) << "missing " << key;
        EXPECT_FALSE(q.at(key).empty()) << "empty " << key;
    }

    EXPECT_EQ("code", q.at("response_type"));
    EXPECT_EQ("S256", q.at("code_challenge_method")) << "plain PKCE would defeat the challenge";
}

TEST(OktaAuthorizeUrl, PkceChallengeIsBase64UrlOfASha256Digest)
{
    const auto q = queryOf(configured(baseConfig()).buildAuthorizeUrl().authorizeUrl);
    const std::string challenge = q.at("code_challenge");

    // 32 raw bytes, unpadded base64url: 43 characters from an alphabet that needs no escaping.
    EXPECT_EQ(43u, challenge.size());
    EXPECT_EQ(std::string::npos, challenge.find_first_not_of(
                                     "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"))
        << "challenge must survive a query string without escaping: " << challenge;
}

TEST(OktaAuthorizeUrl, ReturnedStateIsTheOneInTheUrl)
{
    const auto r = configured(baseConfig()).buildAuthorizeUrl();
    ASSERT_TRUE(r.success) << r.error;

    // The caller stores r.state to match the callback against; if it ever diverged from the URL,
    // every login would fail closed — or worse, be matched against the wrong pending transaction.
    EXPECT_EQ(r.state, queryOf(r.authorizeUrl).at("state"));
    EXPECT_EQ(32u, r.state.size());
    EXPECT_TRUE(isLowerHex(r.state)) << r.state;
}

TEST(OktaAuthorizeUrl, EveryCallMintsFreshStateNonceAndChallenge)
{
    auto client = configured(baseConfig());

    const auto first = client.buildAuthorizeUrl();
    const auto second = client.buildAuthorizeUrl();
    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);

    EXPECT_NE(first.state, second.state);
    EXPECT_NE(queryOf(first.authorizeUrl).at("nonce"), queryOf(second.authorizeUrl).at("nonce"));
    EXPECT_NE(queryOf(first.authorizeUrl).at("code_challenge"), queryOf(second.authorizeUrl).at("code_challenge"))
        << "a reused verifier makes PKCE decorative";
}

// ── buildAuthorizeUrl: the issuer becomes the origin ────────────────────────────────────────────

TEST(OktaAuthorizeUrl, OmitsTheDefaultPortAndKeepsAnyOther)
{
    auto cfg = baseConfig();
    cfg.issuer = "https://dev-123.okta.com/oauth2/default";
    EXPECT_EQ("https://dev-123.okta.com/oauth2/default/v1/authorize",
              originOf(configured(cfg).buildAuthorizeUrl().authorizeUrl));

    cfg.issuer = "https://dev-123.okta.com:443/oauth2/default";
    EXPECT_EQ("https://dev-123.okta.com/oauth2/default/v1/authorize",
              originOf(configured(cfg).buildAuthorizeUrl().authorizeUrl))
        << "an explicit :443 is still the default port";

    cfg.issuer = "https://dev-123.okta.com:8443/oauth2/default";
    EXPECT_EQ("https://dev-123.okta.com:8443/oauth2/default/v1/authorize",
              originOf(configured(cfg).buildAuthorizeUrl().authorizeUrl));
}

TEST(OktaAuthorizeUrl, HandlesAnIssuerWithNoPathAndOneWithATrailingSlash)
{
    auto cfg = baseConfig();

    cfg.issuer = "https://dev-123.okta.com";
    EXPECT_EQ("https://dev-123.okta.com/v1/authorize", originOf(configured(cfg).buildAuthorizeUrl().authorizeUrl));

    cfg.issuer = "https://dev-123.okta.com/oauth2/default/";
    EXPECT_EQ("https://dev-123.okta.com/oauth2/default/v1/authorize",
              originOf(configured(cfg).buildAuthorizeUrl().authorizeUrl))
        << "a trailing slash must not produce a doubled separator";
}

// ── buildAuthorizeUrl: encoding ─────────────────────────────────────────────────────────────────

TEST(OktaAuthorizeUrl, PercentEncodesTheRedirectUri)
{
    const auto q = queryOf(configured(baseConfig()).buildAuthorizeUrl().authorizeUrl);

    // Left raw, the ':' and '/' of the redirect URI would read as structure to whatever parses the
    // query next.
    EXPECT_EQ("https%3A%2F%2Fpretzel.local%2Fapi%2Fauth%2Foidc%2Fcallback", q.at("redirect_uri"));
}

TEST(OktaAuthorizeUrl, PercentEncodesTheSpacesBetweenScopes)
{
    const auto q = queryOf(configured(baseConfig()).buildAuthorizeUrl().authorizeUrl);
    EXPECT_EQ("openid%20email%20profile", q.at("scope"));
}

TEST(OktaAuthorizeUrl, PercentEncodesAClientIdContainingQueryDelimiters)
{
    auto cfg = baseConfig();
    cfg.clientId = "id&injected=1";

    const auto r = configured(cfg).buildAuthorizeUrl();
    ASSERT_TRUE(r.success) << r.error;

    const auto q = queryOf(r.authorizeUrl);
    EXPECT_EQ("id%26injected%3D1", q.at("client_id"));
    EXPECT_EQ(0u, q.count("injected")) << "an unescaped '&' would have smuggled in a parameter";
}

// ── exchangeAndVerify: the guards that run before any network call ──────────────────────────────

TEST(OktaExchange, RefusesWhenOidcIsDisabled)
{
    auto cfg = baseConfig();
    cfg.enabled = false;

    const auto r = configured(cfg).exchangeAndVerify("any-code", "any-state");
    EXPECT_FALSE(r.success);
    EXPECT_EQ("oidc disabled", r.error);
}

TEST(OktaExchange, RejectsAStateItNeverIssued)
{
    auto client = configured(baseConfig());
    client.buildAuthorizeUrl();   // a pending transaction exists, just not this one

    const auto r = client.exchangeAndVerify("code", "0123456789abcdef0123456789abcdef");
    EXPECT_FALSE(r.success);
    EXPECT_EQ("unknown or expired state", r.error) << "an unmatched state is how a forged callback arrives";
}

TEST(OktaExchange, RejectsAnEmptyState)
{
    auto client = configured(baseConfig());
    client.buildAuthorizeUrl();

    const auto r = client.exchangeAndVerify("code", "");
    EXPECT_FALSE(r.success);
    EXPECT_EQ("unknown or expired state", r.error);
}

TEST(OktaExchange, ConsumesTheStateSoACallbackCannotBeReplayed)
{
    auto client = configured(unreachableIdp());

    const auto start = client.buildAuthorizeUrl();
    ASSERT_TRUE(start.success) << start.error;

    // First use gets past the state check and dies at the token endpoint, which is unreachable by
    // construction — the point is only that the transaction was spent on the way through.
    const auto first = client.exchangeAndVerify("code", start.state);
    EXPECT_FALSE(first.success);
    EXPECT_NE("unknown or expired state", first.error) << "the state should have been accepted: " << first.error;

    const auto replay = client.exchangeAndVerify("code", start.state);
    EXPECT_FALSE(replay.success);
    EXPECT_EQ("unknown or expired state", replay.error) << "a spent authorization code must not be retryable";
}

TEST(OktaExchange, ReportsTheTransportFailureRatherThanASilentFalse)
{
    auto client = configured(unreachableIdp());
    const auto start = client.buildAuthorizeUrl();
    ASSERT_TRUE(start.success) << start.error;

    const auto r = client.exchangeAndVerify("code", start.state);
    ASSERT_FALSE(r.success);

    // Whatever went wrong, the operator needs more than "login failed" in the log.
    EXPECT_NE(std::string::npos, r.error.find("token endpoint failed")) << r.error;
    EXPECT_LT(std::string("token endpoint failed: ").size(), r.error.size()) << "no detail after the prefix";
}

// OktaClient::configure() logs, and LOG_* dereferences the logger without checking it — an
// uninitialised one is a null shared_ptr, so the first enabled config would segfault. Initialise it
// into a scratch file and mute it: this suite asserts on return values, not on log lines.
int main(int argc, char** argv)
{
    const auto logFile = std::filesystem::temp_directory_path() / "pretzel-tests" / "okta-client.log";
    pz::util::Logger::Init("test-okta-client", logFile.string(), 256 * 1024, 1);
    pz::util::Logger::GetLogger()->set_level(spdlog::level::off);

    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();

    pz::util::Logger::Shutdown();
    return rc;
}
