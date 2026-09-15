// The four helpers every mgmtd web handler is built out of.
//
// parseBody is the one with teeth. Each controller used to write its own version and every copy
// stopped one step short: it checked that the body PARSED, not that it was an object. `[]`, `42`
// and `null` all parse, and the next line is invariably `input.value("...")` — which is an object
// accessor and throws on anything else. There is no try/catch between a handler and the socket, so
// a well-formed body of the wrong shape was a crash rather than a 400.

#include "service/web/WebUtil.h"

#include <gtest/gtest.h>

#include <string>

using namespace pz::mgmtd;
using pz::http::HttpRequest;
using pz::http::HttpResponse;
using json = nlohmann::json;

namespace
{

HttpRequest withBody(const std::string& body)
{
    HttpRequest req;
    req.method = "POST";
    req.target = "/api/whatever";
    req.body = body;
    return req;
}

}

// ── parseBody ───────────────────────────────────────────────────────────────────────────────────

TEST(ParseBody, AcceptsAJsonObjectAndHandsItOver)
{
    HttpResponse resp;
    json out;

    ASSERT_TRUE(parseBody(withBody(R"({"target":"10.0.0.1","port":8443})"), resp, out));
    EXPECT_EQ("10.0.0.1", out.value("target", std::string()));
    EXPECT_EQ(8443, out.value("port", 0));
}

TEST(ParseBody, AcceptsAnEmptyObject)
{
    // A handler with only optional fields is entitled to an empty body.
    HttpResponse resp;
    json out;

    ASSERT_TRUE(parseBody(withBody("{}"), resp, out));
    EXPECT_TRUE(out.is_object());
    EXPECT_TRUE(out.empty());
}

TEST(ParseBody, RefusesABodyThatIsNotJsonAtAll)
{
    for (const char* body : {"", "not json", "{", R"({"a":})", "{'a':1}"})
    {
        HttpResponse resp;
        json out;

        EXPECT_FALSE(parseBody(withBody(body), resp, out)) << "body '" << body << "'";
        EXPECT_EQ(400, resp.status) << "body '" << body << "'";
        EXPECT_EQ(R"({"error":"invalid JSON body"})", resp.body) << "body '" << body << "'";
    }
}

TEST(ParseBody, RefusesWellFormedJsonThatIsNotAnObject)
{
    // The case the per-controller copies missed. Each of these parses, and each one throws out of
    // the first value() call a handler makes — nlohmann answers type_error.306 for every one.
    for (const char* body : {"[]", "[1,2]", R"("a string")", "42", "true", "null"})
    {
        HttpResponse resp;
        json out;

        EXPECT_FALSE(parseBody(withBody(body), resp, out)) << "body '" << body << "'";
        EXPECT_EQ(400, resp.status) << "body '" << body << "'";
        EXPECT_EQ(R"({"error":"request body must be a JSON object"})", resp.body) << "body '" << body << "'";
    }
}

TEST(ParseBody, SaysWhichOfTheTwoWaysABodyWasWrong)
{
    // Worth distinguishing: "your JSON is broken" and "your JSON is fine but I wanted an object"
    // send an integrator to different places.
    HttpResponse malformed;
    HttpResponse wrongShape;
    json out;

    parseBody(withBody("{"), malformed, out);
    parseBody(withBody("[]"), wrongShape, out);

    EXPECT_NE(malformed.body, wrongShape.body);
}

TEST(ParseBody, AnswersWithJsonSoTheBrowserCanReadTheError)
{
    HttpResponse resp;
    json out;

    ASSERT_FALSE(parseBody(withBody("["), resp, out));
    EXPECT_EQ("application/json; charset=utf-8", resp.contentType);
    EXPECT_FALSE(json::parse(resp.body, nullptr, false).is_discarded()) << resp.body;
}

TEST(ParseBody, DoesNotLeaveAPreviousValueBehindOnFailure)
{
    // A handler that forgot to check the return value must not find last request's object sitting
    // in `out` and act on it.
    HttpResponse resp;
    json out = json{{"target", "10.0.0.1"}};

    ASSERT_FALSE(parseBody(withBody("nonsense"), resp, out));
    EXPECT_FALSE(out.is_object()) << out.dump();
}

// ── fill ────────────────────────────────────────────────────────────────────────────────────────

TEST(Fill, SetsStatusBodyAndDefaultsToJson)
{
    HttpResponse resp;
    fill(resp, 202, R"({"ticket":7})");

    EXPECT_EQ(202, resp.status);
    EXPECT_EQ(R"({"ticket":7})", resp.body);
    EXPECT_EQ("application/json; charset=utf-8", resp.contentType);
}

TEST(Fill, TakesAnExplicitContentTypeForTheRoutesThatAreNotJson)
{
    HttpResponse resp;
    fill(resp, 200, "ok\n", "text/plain; charset=utf-8");

    EXPECT_EQ("text/plain; charset=utf-8", resp.contentType);
}

// ── queryParam ──────────────────────────────────────────────────────────────────────────────────

TEST(QueryParam, ReadsAValueByName)
{
    EXPECT_EQ("7", queryParam("/api/result?ticket=7", "ticket"));
    EXPECT_EQ("b", queryParam("/api/x?a=a&b=b&c=c", "b"));
    EXPECT_EQ("c", queryParam("/api/x?a=a&b=b&c=c", "c")) << "the last parameter is reachable too";
}

TEST(QueryParam, IsEmptyWhenTheKeyOrTheQueryIsAbsent)
{
    EXPECT_EQ("", queryParam("/api/result?ticket=7", "missing"));
    EXPECT_EQ("", queryParam("/api/result", "ticket"));
    EXPECT_EQ("", queryParam("", "ticket"));
    EXPECT_EQ("", queryParam("/api/result?", "ticket"));
}

TEST(QueryParam, DecodesTheValue)
{
    // Filters carry spaces and punctuation; a raw value would not survive the address bar.
    EXPECT_EQ("a b", queryParam("/api/logs?q=a+b", "q"));
    EXPECT_EQ("a b", queryParam("/api/logs?q=a%20b", "q"));
    EXPECT_EQ("level=err", queryParam("/api/logs?q=level%3Derr", "q"));
}

TEST(QueryParam, MatchesTheWholeKeyRatherThanAPrefix)
{
    // "ticket" must not be answered by "ticket_id", or a poll would read the wrong field.
    EXPECT_EQ("", queryParam("/api/x?ticket_id=7", "ticket"));
    EXPECT_EQ("7", queryParam("/api/x?ticket_id=9&ticket=7", "ticket"));
}

TEST(QueryParam, AnEmptyValueIsEmptyNotAbsent)
{
    EXPECT_EQ("", queryParam("/api/x?q=", "q"));
}

// ── sessionCookie ───────────────────────────────────────────────────────────────────────────────

TEST(SessionCookie, ReadsTheSessionValue)
{
    HttpRequest req;
    req.cookie = "session=deadbeef";
    EXPECT_EQ("deadbeef", sessionCookie(req));

    req.cookie = "session=deadbeef; Path=/";
    EXPECT_EQ("deadbeef", sessionCookie(req));
}

TEST(SessionCookie, FindsItAmongOtherCookies)
{
    HttpRequest req;
    req.cookie = "theme=dark; session=deadbeef; tab=devices";
    EXPECT_EQ("deadbeef", sessionCookie(req));
}

TEST(SessionCookie, IsNotFooledByACookieThatMerelyEndsInSession)
{
    // "pz_session=..." is a different cookie. Matching it would hand the auth gate a value from
    // somewhere else entirely.
    HttpRequest req;
    req.cookie = "pz_session=wrong; session=right";
    EXPECT_EQ("right", sessionCookie(req));

    req.cookie = "other_session=wrong";
    EXPECT_EQ("", sessionCookie(req));
}

TEST(SessionCookie, IsEmptyWhenThereIsNoCookieAtAll)
{
    HttpRequest req;
    EXPECT_EQ("", sessionCookie(req));

    req.cookie = "theme=dark";
    EXPECT_EQ("", sessionCookie(req));
}

TEST(SessionCookie, AnEmptySessionValueComesBackEmpty)
{
    HttpRequest req;
    req.cookie = "session=; theme=dark";
    EXPECT_EQ("", sessionCookie(req));
}
