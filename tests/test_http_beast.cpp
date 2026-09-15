// The boundary between Beast's HTTP types and pretzel's own.
//
// Every request apid and mgmtd serve crosses toRequest() on the way in and toBeastResponse() on the
// way out, and both server sessions — plain and TLS — share this one pair. Two things here are
// decisions rather than copying: the conditional-GET answer (an ETag that matches turns a response
// into a 304 with no body, which is what keeps the console's static assets off the wire) and the
// keep-alive handshake that tells the session whether to close the connection afterwards.

#include "http/HttpBeast.h"

#include <gtest/gtest.h>

#include <string>

namespace http = boost::beast::http;

using pz::http::HttpRequest;
using pz::http::HttpResponse;
using pz::http::detail::BeastRequest;
using pz::http::detail::toBeastResponse;
using pz::http::detail::toRequest;

namespace
{

BeastRequest get(const std::string& target = "/", int version = 11)
{
    BeastRequest req{http::verb::get, target, version};
    req.set(http::field::host, "pretzel.local");
    return req;
}

HttpResponse jsonOk(std::string body = R"({"ok":true})")
{
    HttpResponse out;
    out.status = 200;
    out.body = std::move(body);
    return out;
}

std::string header(const http::response<http::string_body>& res, http::field f)
{
    auto it = res.find(f);
    return it == res.end() ? std::string{} : std::string(it->value());
}

bool hasHeader(const http::response<http::string_body>& res, http::field f)
{
    return res.find(f) != res.end();
}

}

// ── toRequest ───────────────────────────────────────────────────────────────────────────────────

TEST(ToRequest, CarriesTheMethodTargetAndBody)
{
    BeastRequest req{http::verb::post, "/api/devices?x=1", 11};
    req.body() = R"({"name":"fw-1"})";

    const HttpRequest out = toRequest(req);
    EXPECT_EQ("POST", out.method);
    EXPECT_EQ("/api/devices?x=1", out.target) << "the query string is part of the target, not stripped";
    EXPECT_EQ(R"({"name":"fw-1"})", out.body);
}

TEST(ToRequest, PicksUpAuthorizationAndCookie)
{
    auto req = get("/api/me");
    req.set(http::field::authorization, "Bearer abc123");
    req.set(http::field::cookie, "pz_session=deadbeef; Path=/");

    const HttpRequest out = toRequest(req);
    EXPECT_EQ("Bearer abc123", out.authorization);
    EXPECT_EQ("pz_session=deadbeef; Path=/", out.cookie);
}

TEST(ToRequest, LeavesAuthorizationAndCookieEmptyWhenAbsent)
{
    // The routing layer compares these against configured values, so "absent" has to arrive as an
    // empty string rather than as something a comparison might accidentally match.
    const HttpRequest out = toRequest(get("/health"));
    EXPECT_TRUE(out.authorization.empty());
    EXPECT_TRUE(out.cookie.empty());
    EXPECT_TRUE(out.body.empty());
}

TEST(ToRequest, RendersEveryMethodItIsGivenRatherThanOnlyTheKnownOnes)
{
    struct
    {
        http::verb verb;
        const char* name;
    } cases[] = {{http::verb::get, "GET"},   {http::verb::post, "POST"},   {http::verb::put, "PUT"},
                 {http::verb::delete_, "DELETE"}, {http::verb::head, "HEAD"}, {http::verb::patch, "PATCH"},
                 {http::verb::options, "OPTIONS"}};

    for (const auto& c : cases)
    {
        BeastRequest req{c.verb, "/", 11};
        EXPECT_EQ(c.name, toRequest(req).method) << c.name;
    }
}

// ── toBeastResponse: the ordinary answer ────────────────────────────────────────────────────────

TEST(ToBeastResponse, CarriesStatusContentTypeAndBody)
{
    bool close = false;
    const auto res = toBeastResponse(get(), jsonOk(), "pz-mgmtd", close);

    EXPECT_EQ(http::status::ok, res->result());
    EXPECT_EQ("application/json; charset=utf-8", header(*res, http::field::content_type));
    EXPECT_EQ(R"({"ok":true})", res->body());
    EXPECT_EQ("pz-mgmtd", header(*res, http::field::server));
}

TEST(ToBeastResponse, PassesAnyStatusCodeThrough)
{
    for (int status : {200, 202, 302, 400, 401, 404, 500, 503})
    {
        HttpResponse out = jsonOk();
        out.status = status;

        bool close = false;
        EXPECT_EQ(status, static_cast<int>(toBeastResponse(get(), out, "pz-apid", close)->result_int())) << status;
    }
}

TEST(ToBeastResponse, SetsTheOptionalHeadersOnlyWhenTheyAreFilledIn)
{
    HttpResponse out = jsonOk();
    out.setCookie = "pz_session=abc; HttpOnly";
    out.location = "/login";
    out.contentDisposition = "attachment; filename=\"logs.csv\"";

    bool close = false;
    const auto with = toBeastResponse(get(), out, "pz-mgmtd", close);
    EXPECT_EQ("pz_session=abc; HttpOnly", header(*with, http::field::set_cookie));
    EXPECT_EQ("/login", header(*with, http::field::location));
    EXPECT_EQ("attachment; filename=\"logs.csv\"", header(*with, http::field::content_disposition));

    const auto without = toBeastResponse(get(), jsonOk(), "pz-mgmtd", close);
    EXPECT_FALSE(hasHeader(*without, http::field::set_cookie)) << "an empty field must not be sent as blank";
    EXPECT_FALSE(hasHeader(*without, http::field::location));
    EXPECT_FALSE(hasHeader(*without, http::field::content_disposition));
}

TEST(ToBeastResponse, SetsContentLengthFromTheBody)
{
    bool close = false;
    const auto res = toBeastResponse(get(), jsonOk("0123456789"), "pz-mgmtd", close);

    // prepare_payload() is what makes the framing right; without it the client hangs waiting for
    // bytes that never come.
    EXPECT_EQ("10", header(*res, http::field::content_length));
}

// ── toBeastResponse: conditional GET ────────────────────────────────────────────────────────────

TEST(ToBeastResponse, AnEtagIsSentBackWithNoCache)
{
    HttpResponse out = jsonOk();
    out.etag = "\"abc123\"";

    bool close = false;
    const auto res = toBeastResponse(get(), out, "pz-mgmtd", close);

    EXPECT_EQ("\"abc123\"", header(*res, http::field::etag));
    EXPECT_EQ("no-cache", header(*res, http::field::cache_control))
        << "no-cache means revalidate, not don't-store — the 304 path depends on the client asking again";
}

TEST(ToBeastResponse, AMatchingIfNoneMatchBecomesA304WithNoBody)
{
    auto req = get("/js/config.js");
    req.set(http::field::if_none_match, "\"v7\"");

    HttpResponse out = jsonOk("the whole file");
    out.etag = "\"v7\"";

    bool close = false;
    const auto res = toBeastResponse(req, out, "pz-mgmtd", close);

    EXPECT_EQ(http::status::not_modified, res->result());
    EXPECT_TRUE(res->body().empty()) << "a 304 carrying a body defeats the point of answering one";
    EXPECT_FALSE(hasHeader(*res, http::field::content_type));
    EXPECT_EQ("\"v7\"", header(*res, http::field::etag)) << "the validator still has to come back";
}

TEST(ToBeastResponse, ANonMatchingIfNoneMatchSendsTheBody)
{
    auto req = get("/js/config.js");
    req.set(http::field::if_none_match, "\"v6\"");

    HttpResponse out = jsonOk("the whole file");
    out.etag = "\"v7\"";

    bool close = false;
    const auto res = toBeastResponse(req, out, "pz-mgmtd", close);

    EXPECT_EQ(http::status::ok, res->result());
    EXPECT_EQ("the whole file", res->body());
}

TEST(ToBeastResponse, IfNoneMatchIsIgnoredWhenTheResponseCarriesNoEtag)
{
    // Nothing to compare against: a stale validator from some other resource must not turn a fresh
    // answer into a 304.
    auto req = get();
    req.set(http::field::if_none_match, "\"anything\"");

    bool close = false;
    const auto res = toBeastResponse(req, jsonOk("fresh"), "pz-mgmtd", close);

    EXPECT_EQ(http::status::ok, res->result());
    EXPECT_EQ("fresh", res->body());
}

TEST(ToBeastResponse, TheEtagComparisonIsExact)
{
    HttpResponse out = jsonOk("body");
    out.etag = "\"v7\"";

    // A weak validator and a differently-quoted one are not the same string, and this comparison
    // does not pretend otherwise — it answers 200 rather than guessing.
    for (const char* sent : {"W/\"v7\"", "v7", "\"v7\", \"v8\"", "\"V7\""})
    {
        auto req = get();
        req.set(http::field::if_none_match, sent);

        bool close = false;
        EXPECT_EQ(http::status::ok, toBeastResponse(req, out, "pz-mgmtd", close)->result()) << sent;
    }
}

// ── toBeastResponse: keep-alive ─────────────────────────────────────────────────────────────────

TEST(ToBeastResponse, MirrorsTheRequestsKeepAliveAndReportsWhetherToClose)
{
    // HTTP/1.1 defaults to keeping the connection open; the session loops back to another read.
    {
        auto req = get();
        req.keep_alive(true);

        bool close = true;
        const auto res = toBeastResponse(req, jsonOk(), "pz-mgmtd", close);
        EXPECT_TRUE(res->keep_alive());
        EXPECT_FALSE(close);
    }

    // An explicit Connection: close must be honoured, and the session told to shut down.
    {
        auto req = get();
        req.keep_alive(false);

        bool close = false;
        const auto res = toBeastResponse(req, jsonOk(), "pz-mgmtd", close);
        EXPECT_FALSE(res->keep_alive());
        EXPECT_TRUE(close) << "the session reads this flag to decide whether to close the socket";
    }
}

TEST(ToBeastResponse, AnHttp10RequestWithoutKeepAliveClosesTheConnection)
{
    BeastRequest req{http::verb::get, "/", 10};
    req.set(http::field::host, "pretzel.local");

    bool close = false;
    const auto res = toBeastResponse(req, jsonOk(), "pz-mgmtd", close);

    EXPECT_EQ(10u, res->version()) << "the answer must speak the version that was asked";
    EXPECT_TRUE(close);
}

TEST(ToBeastResponse, KeepAliveIsDecidedTheSameWayOnA304)
{
    auto req = get("/js/config.js");
    req.set(http::field::if_none_match, "\"v7\"");
    req.keep_alive(true);

    HttpResponse out = jsonOk("the whole file");
    out.etag = "\"v7\"";

    bool close = true;
    const auto res = toBeastResponse(req, out, "pz-mgmtd", close);

    // A 304 that closed the connection would cost a new handshake for every cached asset — the
    // opposite of what conditional GET is for.
    EXPECT_EQ(http::status::not_modified, res->result());
    EXPECT_TRUE(res->keep_alive());
    EXPECT_FALSE(close);
}
