// collectord's ApiUtil — the shared helpers every device exchange is assembled from.
//
// This is the daemon that reaches outside the appliance, and these functions decide what goes on
// the wire: which host is dialled, which credential is attached, which certificate is pinned, and
// what a vendor's answer is understood to mean. They are pure, take no service manager and open no
// socket, so there is nothing standing between them and an assertion.
//
// The IPC senders in the same unit (sendTestResponse, sendApiCredentialState, ...) need the whole
// daemon behind them and are not touched here; --gc-sections drops them at link time.

#include "service/api/ApiUtil.h"

#include "algorithm/Base64.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using namespace pz::collectord;
using json = nlohmann::json;

namespace
{

std::string headerValue(const pz::http::ClientRequest& req, const std::string& name)
{
    for (const auto& [k, v] : req.headers)
    {
        if (k == name)
            return v;
    }
    return {};
}

pz::http::ClientResponse keygenResponse(int status, std::string body)
{
    pz::http::ClientResponse res;
    res.tlsOk = true;
    res.pinMatched = true;
    res.requestSent = true;
    res.status = status;
    res.body = std::move(body);
    res.fingerprint = "AA:BB:CC";
    return res;
}

TestTarget ngfwTarget()
{
    TestTarget t;
    t.host = "10.0.0.1";
    t.port = 443;
    t.fingerprint = "AA:BB:CC:DD";
    t.username = "admin";
    t.password = "pa55w0rd";
    return t;
}

}

// ── splitHostPath ───────────────────────────────────────────────────────────────────────────────

TEST(ApiUtilSplitHostPath, FallsBackToBothDefaultsOnlyForAnEmptyInput)
{
    const auto hp = splitHostPath("", kSaseAuthHost, kSaseTokenPath);
    EXPECT_EQ(kSaseAuthHost, hp.host);
    EXPECT_EQ(kSaseTokenPath, hp.path);
    EXPECT_EQ(443, hp.port);
}

TEST(ApiUtilSplitHostPath, SplitsAFullUrl)
{
    const auto hp = splitHostPath("https://auth.example.com/oauth2/token", kSaseAuthHost, kSaseTokenPath);
    EXPECT_EQ("auth.example.com", hp.host);
    EXPECT_EQ("/oauth2/token", hp.path);
    EXPECT_EQ(443, hp.port);
}

TEST(ApiUtilSplitHostPath, AcceptsABareHostAndPathWithNoScheme)
{
    // The field this comes from has always taken either form, so dropping the scheme must not
    // change where the request goes.
    const auto withScheme = splitHostPath("https://auth.example.com/x", kSaseAuthHost, kSaseTokenPath);
    const auto without = splitHostPath("auth.example.com/x", kSaseAuthHost, kSaseTokenPath);

    EXPECT_EQ(withScheme.host, without.host);
    EXPECT_EQ(withScheme.path, without.path);
    EXPECT_EQ(withScheme.port, without.port);
}

TEST(ApiUtilSplitHostPath, AHostWithNoPathGetsRootRatherThanTheDefaultPath)
{
    // Pinning a real asymmetry: defaultPath applies to an empty input only. Once the operator has
    // typed a host, the path they did not type is "/", not the caller's default.
    const auto hp = splitHostPath("auth.example.com", kSaseAuthHost, kSaseTokenPath);
    EXPECT_EQ("auth.example.com", hp.host);
    EXPECT_EQ("/", hp.path);
}

TEST(ApiUtilSplitHostPath, ReadsAnExplicitPort)
{
    const auto hp = splitHostPath("https://auth.example.com:8443/token", kSaseAuthHost, kSaseTokenPath);
    EXPECT_EQ("auth.example.com", hp.host);
    EXPECT_EQ(8443, hp.port);
    EXPECT_EQ("/token", hp.path);
}

TEST(ApiUtilSplitHostPath, KeepsTheQueryStringWithThePath)
{
    const auto hp = splitHostPath("https://dev.example.com/api/?type=op&cmd=x", kSaseAuthHost, kSaseTokenPath);
    EXPECT_EQ("dev.example.com", hp.host);
    EXPECT_EQ("/api/?type=op&cmd=x", hp.path);
}

TEST(ApiUtilSplitHostPath, LeavesANonNumericPortAttachedToTheHostAndKeepsTheDefault)
{
    // Pinning current behaviour: "host:abc" is not silently reinterpreted, it stays as typed so the
    // resolve fails loudly instead of quietly dialling the wrong place.
    const auto hp = splitHostPath("https://auth.example.com:abc/token", kSaseAuthHost, kSaseTokenPath);
    EXPECT_EQ("auth.example.com:abc", hp.host);
    EXPECT_EQ(443, hp.port);
}

// ── buildOAuthTokenRequest ──────────────────────────────────────────────────────────────────────

TEST(ApiUtilOAuthRequest, TargetsTheHostAndPathItWasGiven)
{
    const auto hp = splitHostPath("", kSaseAuthHost, kSaseTokenPath);
    const auto req = buildOAuthTokenRequest(hp, "cid", "csecret", "1963594622");

    EXPECT_EQ(kSaseAuthHost, req.host);
    EXPECT_EQ(kSaseTokenPath, req.target);
    EXPECT_EQ(443, req.port);
    EXPECT_EQ("POST", req.method);
}

TEST(ApiUtilOAuthRequest, VerifiesTheCaChainRatherThanPinningACertificate)
{
    const auto req = buildOAuthTokenRequest(splitHostPath("", kSaseAuthHost, kSaseTokenPath), "cid", "cs", "tsg");

    // A public cloud endpoint, not a customer box: pinning here would break on every certificate
    // rotation Palo Alto performs, and an empty pin means "first contact, send nothing".
    EXPECT_TRUE(req.verifyCa);
    EXPECT_TRUE(req.expectedFingerprint.empty());
}

TEST(ApiUtilOAuthRequest, CarriesTheCredentialAsHttpBasic)
{
    const auto req = buildOAuthTokenRequest(splitHostPath("", kSaseAuthHost, kSaseTokenPath), "the-id", "the-secret",
                                            "tsg");

    EXPECT_EQ("Basic " + pz::algorithm::base64Encode(std::string("the-id:the-secret")),
              headerValue(req, "Authorization"));
    EXPECT_EQ("application/x-www-form-urlencoded", headerValue(req, "Content-Type"));
}

TEST(ApiUtilOAuthRequest, AsksForClientCredentialsScopedToTheTenant)
{
    const auto req = buildOAuthTokenRequest(splitHostPath("", kSaseAuthHost, kSaseTokenPath), "cid", "cs",
                                            "1963594622");
    EXPECT_EQ("grant_type=client_credentials&scope=tsg_id:1963594622", req.body);
}

TEST(ApiUtilOAuthRequest, PercentEncodesATenantIdThatWouldOtherwiseBreakTheForm)
{
    const auto req = buildOAuthTokenRequest(splitHostPath("", kSaseAuthHost, kSaseTokenPath), "cid", "cs", "a&b=c");
    EXPECT_EQ("grant_type=client_credentials&scope=tsg_id:a%26b%3Dc", req.body);
}

// ── stepJson ────────────────────────────────────────────────────────────────────────────────────

TEST(ApiUtilStepJson, IsAnOkFlagAndADetailString)
{
    const auto ok = stepJson(true, "TLS established, certificate pinned");
    EXPECT_TRUE(ok["ok"].get<bool>());
    EXPECT_EQ("TLS established, certificate pinned", ok["detail"].get<std::string>());

    const auto bad = stepJson(false, "");
    EXPECT_FALSE(bad["ok"].get<bool>());
    EXPECT_EQ("", bad["detail"].get<std::string>());
}

// ── parseTestTarget ─────────────────────────────────────────────────────────────────────────────

TEST(ApiUtilParseTarget, DefaultsToPort443AndNgfw)
{
    const auto t = parseTestTarget(json{{"target", "10.0.0.1"}});
    EXPECT_EQ("10.0.0.1", t.host);
    EXPECT_EQ(443, t.port);
    EXPECT_EQ("ngfw", t.deviceType);
}

TEST(ApiUtilParseTarget, SplitsAnExplicitPortOffTheTarget)
{
    const auto t = parseTestTarget(json{{"target", "10.0.0.1:8443"}});
    EXPECT_EQ("10.0.0.1", t.host);
    EXPECT_EQ(8443, t.port);
}

TEST(ApiUtilParseTarget, LeavesABracketedIpv6LiteralAlone)
{
    // "[fe80::1]" is a host, not a host and a port. Splitting on the last colon would leave a
    // truncated address and a nonsense port.
    const auto t = parseTestTarget(json{{"target", "[fe80::1]"}});
    EXPECT_EQ("[fe80::1]", t.host);
    EXPECT_EQ(443, t.port);
}

TEST(ApiUtilParseTarget, LeavesAnUnbracketedIpv6LiteralAloneToo)
{
    // More than one colon means this cannot be host:port, so it is taken whole.
    const auto t = parseTestTarget(json{{"target", "fe80::1"}});
    EXPECT_EQ("fe80::1", t.host);
    EXPECT_EQ(443, t.port);
}

TEST(ApiUtilParseTarget, ReadsCredentialsFromTheSecretsObject)
{
    const auto t = parseTestTarget(json{{"target", "10.0.0.1"},
                                        {"secrets", {{"username", "admin"}, {"password", "pa55"}}}});
    EXPECT_EQ("admin", t.username);
    EXPECT_EQ("pa55", t.password);
}

TEST(ApiUtilParseTarget, FallsBackToATopLevelUsernameWhenSecretsOmitsOne)
{
    const auto t = parseTestTarget(json{{"target", "10.0.0.1"}, {"username", "legacy-admin"}});
    EXPECT_EQ("legacy-admin", t.username);
    EXPECT_EQ("", t.password);
}

TEST(ApiUtilParseTarget, SurvivesASecretsFieldThatIsNotAnObject)
{
    // nlohmann's value() throws on a non-object. A malformed payload has to fail the test, not
    // take the daemon down with it.
    for (const json secrets : {json("a string"), json(42), json::array({1, 2}), json(nullptr)})
    {
        json body{{"target", "10.0.0.1"}, {"username", "fallback"}};
        body["secrets"] = secrets;

        TestTarget t;
        ASSERT_NO_THROW(t = parseTestTarget(body)) << secrets.dump();
        EXPECT_EQ("fallback", t.username) << secrets.dump();
    }
}

TEST(ApiUtilParseTarget, TakesTheKeygenEndpointFromEitherFieldName)
{
    EXPECT_EQ("/api/?type=keygen", parseTestTarget(json{{"keygen_endpoint", "/api/?type=keygen"}}).keygenEndpoint);
    EXPECT_EQ("/api/", parseTestTarget(json{{"endpoint", "/api/"}}).keygenEndpoint);

    // keygen_endpoint wins when both are present — the endpoint field is the generic one.
    const auto both = parseTestTarget(json{{"keygen_endpoint", "/keygen"}, {"endpoint", "/other"}});
    EXPECT_EQ("/keygen", both.keygenEndpoint);
}

TEST(ApiUtilParseTarget, CarriesThePinAndTheProfileOid)
{
    const auto t = parseTestTarget(json{{"target", "10.0.0.1"},
                                        {"fingerprint", "AA:BB:CC"},
                                        {"api_key_oid", "key-1"},
                                        {"device_type", "sase"}});
    EXPECT_EQ("AA:BB:CC", t.fingerprint);
    EXPECT_EQ("key-1", t.authProfileOid);
    EXPECT_EQ("sase", t.deviceType);
}

TEST(ApiUtilParseTarget, AnEmptyPayloadYieldsAnEmptyTargetRatherThanThrowing)
{
    TestTarget t;
    ASSERT_NO_THROW(t = parseTestTarget(json::object()));
    EXPECT_TRUE(t.host.empty());
    EXPECT_EQ(443, t.port);
    EXPECT_EQ("ngfw", t.deviceType);
}

// ── baseRequest ─────────────────────────────────────────────────────────────────────────────────

TEST(ApiUtilBaseRequest, CarriesTheTargetAndItsPin)
{
    const auto req = baseRequest(ngfwTarget());

    EXPECT_EQ("10.0.0.1", req.host);
    EXPECT_EQ(443, req.port);
    EXPECT_EQ("AA:BB:CC:DD", req.expectedFingerprint);
    EXPECT_FALSE(req.verifyCa) << "a customer device is pinned, not chain-verified";
    EXPECT_EQ(std::chrono::seconds(10), req.timeout);
}

TEST(ApiUtilBaseRequest, AnUnpinnedTargetProducesAnEmptyPinSoTheFirstContactSendsNothing)
{
    auto t = ngfwTarget();
    t.fingerprint.clear();

    EXPECT_TRUE(baseRequest(t).expectedFingerprint.empty());
}

// ── extractXmlTag / xmlErrorMessage ─────────────────────────────────────────────────────────────

TEST(ApiUtilXml, ExtractsTheContentBetweenTheTags)
{
    EXPECT_EQ("LUFRPT1", extractXmlTag("<response status=\"success\"><result><key>LUFRPT1</key></result></response>",
                                       "key"));
}

TEST(ApiUtilXml, ReturnsEmptyWhenEitherTagIsMissing)
{
    EXPECT_EQ("", extractXmlTag("<response><result/></response>", "key"));
    EXPECT_EQ("", extractXmlTag("<key>unterminated", "key"));
    EXPECT_EQ("", extractXmlTag("", "key"));
}

TEST(ApiUtilXml, TakesTheFirstOccurrence)
{
    EXPECT_EQ("one", extractXmlTag("<k>one</k><k>two</k>", "k"));
}

TEST(ApiUtilXml, DoesNotMatchATagThatMerelyStartsTheSameWay)
{
    // "<keyring>" must not satisfy a search for "<key>", or a keygen response carrying an unrelated
    // element would be read as an issued key.
    EXPECT_EQ("", extractXmlTag("<keyring>not-a-key</keyring>", "key"));
}

TEST(ApiUtilXml, ErrorMessageUsesTheDevicesOwnWordsWhenItSuppliesThem)
{
    EXPECT_EQ("Invalid credentials.", xmlErrorMessage("<response status=\"error\"><msg>Invalid credentials.</msg>"
                                                      "</response>"));
}

TEST(ApiUtilXml, ErrorMessageFallsBackWhenTheDeviceSaysNothingUseful)
{
    EXPECT_EQ("device rejected the request", xmlErrorMessage("<response status=\"error\"/>"));
    EXPECT_EQ("device rejected the request", xmlErrorMessage(""));
    EXPECT_EQ("device rejected the request", xmlErrorMessage("<response><msg></msg></response>"));
}

// ── buildKeygenRequest ──────────────────────────────────────────────────────────────────────────

TEST(ApiUtilKeygenRequest, UsesThePanOsDefaultPathWhenNoneIsConfigured)
{
    auto t = ngfwTarget();
    t.keygenEndpoint.clear();

    EXPECT_EQ("/api/?type=keygen&user=admin&password=pa55w0rd", buildKeygenRequest(t).target);
}

TEST(ApiUtilKeygenRequest, AppendsWithAQuestionMarkWhenTheEndpointHasNoQuery)
{
    auto t = ngfwTarget();
    t.keygenEndpoint = "/api/keygen";

    EXPECT_EQ("/api/keygen?user=admin&password=pa55w0rd", buildKeygenRequest(t).target);
}

TEST(ApiUtilKeygenRequest, AppendsWithAnAmpersandWhenTheEndpointAlreadyHasAQuery)
{
    auto t = ngfwTarget();
    t.keygenEndpoint = "/api/?type=keygen&vsys=vsys1";

    // A second '?' would make everything after it part of the previous parameter's value, and the
    // device would answer "missing user".
    EXPECT_EQ("/api/?type=keygen&vsys=vsys1&user=admin&password=pa55w0rd", buildKeygenRequest(t).target);
}

TEST(ApiUtilKeygenRequest, PercentEncodesCredentialsThatWouldOtherwiseSplitTheQuery)
{
    auto t = ngfwTarget();
    t.keygenEndpoint.clear();
    t.username = "adm in";
    t.password = "p&w=rd";

    const auto req = buildKeygenRequest(t);
    EXPECT_NE(std::string::npos, req.target.find("user=adm%20in"));
    EXPECT_NE(std::string::npos, req.target.find("password=p%26w%3Drd"));
}

TEST(ApiUtilKeygenRequest, KeepsThePinFromTheBaseRequest)
{
    EXPECT_EQ("AA:BB:CC:DD", buildKeygenRequest(ngfwTarget()).expectedFingerprint);
}

// ── readKeygenKey ───────────────────────────────────────────────────────────────────────────────

TEST(ApiUtilReadKeygenKey, ReturnsTheIssuedKeyAndRecordsBothSteps)
{
    json out;
    const std::string key =
        readKeygenKey(keygenResponse(200, "<response status=\"success\"><result><key>LUFRPT1abc</key></result>"
                                          "</response>"),
                      true, out);

    EXPECT_EQ("LUFRPT1abc", key);
    EXPECT_TRUE(out["steps"]["tls"]["ok"].get<bool>());
    EXPECT_TRUE(out["steps"]["auth"]["ok"].get<bool>());
}

TEST(ApiUtilReadKeygenKey, StopsAtTheTlsStepWhenTheHandshakeFailed)
{
    pz::http::ClientResponse res;
    res.error = "connect: Connection refused";

    json out;
    EXPECT_EQ("", readKeygenKey(res, true, out));
    EXPECT_FALSE(out["steps"]["tls"]["ok"].get<bool>());
    EXPECT_FALSE(out["steps"].contains("auth")) << "no credential was sent, so there is no auth step to report";
}

TEST(ApiUtilReadKeygenKey, StopsAtTheTlsStepOnAFirstContactThatWasNotYetTrusted)
{
    pz::http::ClientResponse res;
    res.tlsOk = true;
    res.fingerprint = "AA:BB:CC";   // handshake happened, nothing was written

    json out;
    EXPECT_EQ("", readKeygenKey(res, false, out));
    EXPECT_FALSE(out["steps"]["tls"]["ok"].get<bool>());
    EXPECT_FALSE(out["fingerprint_trusted"].get<bool>());
    EXPECT_EQ("AA:BB:CC", out["fingerprint"].get<std::string>()) << "the operator needs the pin to confirm";
}

TEST(ApiUtilReadKeygenKey, SurfacesTheDevicesOwnRejectionMessage)
{
    json out;
    EXPECT_EQ("", readKeygenKey(keygenResponse(403, "<response status=\"error\"><msg>Invalid credentials.</msg>"
                                                    "</response>"),
                                true, out));

    EXPECT_FALSE(out["steps"]["auth"]["ok"].get<bool>());
    EXPECT_EQ("Invalid credentials.", out["message"].get<std::string>());
    EXPECT_NE(std::string::npos, out["steps"]["auth"]["detail"].get<std::string>().find("403"));
}

TEST(ApiUtilReadKeygenKey, TreatsA200WithNoKeyAsAFailure)
{
    // PAN-OS answers some rejections with 200 and an error document, so the status alone is not
    // enough to say a key was issued.
    json out;
    EXPECT_EQ("", readKeygenKey(keygenResponse(200, "<response status=\"error\"><msg>User not allowed</msg>"
                                                    "</response>"),
                                true, out));

    EXPECT_FALSE(out["steps"]["auth"]["ok"].get<bool>());
    EXPECT_EQ("User not allowed", out["message"].get<std::string>());
}
