// apid's HTTP surface. It is small — a health check and one authenticated ingest endpoint — but it
// is the only part of pretzel that an unauthenticated stranger can reach, so what it answers to a
// request it does not recognise matters as much as what it answers to one it does.
//
// routeIngest() is pure by construction: (request, expected token) in, response out. No service
// manager, no router, no socket.

#include "service/ingest/IngestRouting.h"

#include "util/Logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using pz::apid::bearerToken;
using pz::apid::routeIngest;
using pz::http::HttpRequest;
using pz::http::HttpResponse;

namespace
{

constexpr const char* kToken = "s3cr3t-ingest-token";

HttpRequest egressPost(const std::string& authorization, const std::string& body = R"({"ip":"1.2.3.4"})")
{
    HttpRequest req;
    req.method = "POST";
    req.target = "/api/probe/egress";
    req.authorization = authorization;
    req.body = body;
    return req;
}

HttpResponse route(const HttpRequest& req, const std::string& token = kToken)
{
    HttpResponse resp;   // default-constructed: 404, as an unmatched request must stay
    routeIngest(req, token, resp);
    return resp;
}

}

// ── bearerToken ─────────────────────────────────────────────────────────────────────────────────

TEST(BearerToken, ExtractsTheTokenAfterTheScheme)
{
    EXPECT_EQ("abc123", bearerToken("Bearer abc123"));
    EXPECT_EQ("a b c", bearerToken("Bearer a b c")) << "the value is taken whole, spaces included";
}

TEST(BearerToken, RejectsAnythingThatIsNotABearerHeader)
{
    for (const char* header : {"", "abc123", "Basic abc123", "bearer abc123", "BEARER abc123", "Bearer", "XBearer x"})
    {
        EXPECT_TRUE(bearerToken(header).empty()) << "header '" << header << "' yielded '" << bearerToken(header) << "'";
    }
}

TEST(BearerToken, AnEmptyBearerValueIsEmptyNotTheWholeHeader)
{
    // "Bearer " with nothing after it must not come back as something a comparison could match.
    EXPECT_EQ("", bearerToken("Bearer "));
}

// ── health ──────────────────────────────────────────────────────────────────────────────────────

TEST(IngestRouting, HealthAnswersOkWithoutAnyCredential)
{
    HttpRequest req;
    req.method = "GET";
    req.target = "/health";

    const auto resp = route(req);
    EXPECT_EQ(200, resp.status);
    EXPECT_EQ("ok\n", resp.body);
    EXPECT_EQ("text/plain; charset=utf-8", resp.contentType);
}

TEST(IngestRouting, HealthIsGetOnly)
{
    for (const char* method : {"POST", "PUT", "DELETE", "HEAD"})
    {
        HttpRequest req;
        req.method = method;
        req.target = "/health";
        EXPECT_EQ(404, route(req).status) << method;
    }
}

// ── authentication on the ingest endpoint ───────────────────────────────────────────────────────

TEST(IngestRouting, AcceptsAReportCarryingTheConfiguredToken)
{
    const auto resp = route(egressPost(std::string("Bearer ") + kToken));
    EXPECT_EQ(202, resp.status);
    EXPECT_EQ(R"({"status":"accepted"})", resp.body);
}

TEST(IngestRouting, RejectsAMissingOrWrongToken)
{
    EXPECT_EQ(401, route(egressPost("")).status) << "no Authorization header";
    EXPECT_EQ(401, route(egressPost("Bearer ")).status) << "empty bearer value";
    EXPECT_EQ(401, route(egressPost("Bearer wrong-token")).status);
    EXPECT_EQ(401, route(egressPost(std::string("Basic ") + kToken)).status) << "right secret, wrong scheme";
}

TEST(IngestRouting, RejectsATokenThatIsMerelyAPrefixOrExtensionOfTheRealOne)
{
    const std::string token = kToken;

    EXPECT_EQ(401, route(egressPost("Bearer " + token.substr(0, token.size() - 1))).status) << "truncated";
    EXPECT_EQ(401, route(egressPost("Bearer " + token + "x")).status) << "extended";
    EXPECT_EQ(401, route(egressPost("Bearer " + token + " ")).status) << "trailing space";
}

TEST(IngestRouting, AnUnconfiguredTokenLocksTheEndpointRatherThanOpeningIt)
{
    // A blank configured token is a misconfiguration. Reading it as "no authentication required"
    // would turn a deployment mistake into an open ingest endpoint.
    EXPECT_EQ(401, route(egressPost("Bearer anything"), "").status);
    EXPECT_EQ(401, route(egressPost(""), "").status);
}

TEST(IngestRouting, AuthenticationIsCheckedBeforeTheBodyIsParsed)
{
    // An unauthenticated caller must not be able to tell valid JSON from invalid by the status it
    // gets back, and must not reach the parser at all.
    EXPECT_EQ(401, route(egressPost("Bearer wrong", "{ this is not json")).status);
    EXPECT_EQ(401, route(egressPost("Bearer wrong", "")).status);
}

// ── body handling ───────────────────────────────────────────────────────────────────────────────

TEST(IngestRouting, RejectsABodyThatIsNotJson)
{
    const auto resp = route(egressPost(std::string("Bearer ") + kToken, "{ not json"));
    EXPECT_EQ(400, resp.status);
    EXPECT_EQ(R"({"error":"invalid json"})", resp.body);
}

TEST(IngestRouting, RejectsJsonThatIsNotAnObject)
{
    // Valid JSON, wrong shape: a bare array or scalar has no fields to read, so it is a 400 rather
    // than an accepted report with everything blank.
    for (const char* body : {"[]", R"(["a"])", "42", R"("a string")", "true", "null"})
    {
        EXPECT_EQ(400, route(egressPost(std::string("Bearer ") + kToken, body)).status) << "body " << body;
    }
}

TEST(IngestRouting, AcceptsAnObjectWhoseFieldsAreAllMissing)
{
    // The fields are read with defaults, so an empty object is a valid — if uninformative — report.
    EXPECT_EQ(202, route(egressPost(std::string("Bearer ") + kToken, "{}")).status);
}

TEST(IngestRouting, AcceptsAFullReport)
{
    const std::string body = R"({"path":"mu","tenant":"1963594622","device_id":"fw-1","ip":"203.0.113.9"})";
    EXPECT_EQ(202, route(egressPost(std::string("Bearer ") + kToken, body)).status);
}

// ── everything else ─────────────────────────────────────────────────────────────────────────────

TEST(IngestRouting, LeavesTheResponseUntouchedForAnUnknownRoute)
{
    for (const char* target : {"/", "/api", "/api/probe", "/api/probe/egress/", "/API/PROBE/EGRESS", "/healthz"})
    {
        HttpRequest req;
        req.method = "POST";
        req.target = target;
        req.authorization = std::string("Bearer ") + kToken;

        const auto resp = route(req);
        EXPECT_EQ(404, resp.status) << target;
        EXPECT_EQ(R"({"error":"not found"})", resp.body) << target;
    }
}

TEST(IngestRouting, MatchesTheTargetExactlyRatherThanByPrefix)
{
    HttpRequest req;
    req.method = "POST";
    req.target = "/api/probe/egress?x=1";
    req.authorization = std::string("Bearer ") + kToken;

    // Pinning current behaviour: the route compares whole strings, so a query string does not
    // reach the handler. If apid ever needs query parameters, this is the assertion to revisit.
    EXPECT_EQ(404, route(req).status);
}

TEST(IngestRouting, IngestEndpointIsPostOnly)
{
    for (const char* method : {"GET", "PUT", "DELETE"})
    {
        HttpRequest req;
        req.method = method;
        req.target = "/api/probe/egress";
        req.authorization = std::string("Bearer ") + kToken;
        EXPECT_EQ(404, route(req).status) << method;
    }
}

// routeIngest() logs on both the rejection and the acceptance path, and LOG_* dereferences the
// logger without checking it. Initialise it into a scratch file and mute it.
int main(int argc, char** argv)
{
    const auto logFile = std::filesystem::temp_directory_path() / "pretzel-tests" / "ingest-routing.log";
    pz::util::Logger::Init("test-ingest-routing", logFile.string(), 256 * 1024, 1);
    pz::util::Logger::GetLogger()->set_level(spdlog::level::off);

    ::testing::InitGoogleTest(&argc, argv);
    const int rc = RUN_ALL_TESTS();

    pz::util::Logger::Shutdown();
    return rc;
}
