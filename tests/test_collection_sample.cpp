// Covers pz::scand::buildCollectionSample — the api_collection row scand builds from one device
// call and ships to engined.
//
// The point of the function is to turn a low-level ClientResponse (which distinguishes a TLS
// failure, a pin-gate refusal that never sent the request, and an HTTP error) into the one document
// the collector stores. The rules worth pinning: `ok` means a usable 200 and nothing less;
// http_status appears only when the request was actually sent; and an oversized body is cut to the
// cap with `truncated` set while `bytes` still reports the full size.

#include "service/api/CollectionSample.h"

#include "http/HttpClient.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

using nlohmann::json;
using pz::http::ClientResponse;
using pz::scand::buildCollectionSample;

namespace
{

constexpr std::size_t kCap = 100;

// A clean, trusted 200 with a short body.
ClientResponse okResponse()
{
    ClientResponse r;
    r.tlsOk = true;
    r.pinMatched = true;
    r.requestSent = true;
    r.status = 200;
    r.body = "<response status=\"success\"/>";
    return r;
}

json build(const ClientResponse& r, long long latencyMs = 42)
{
    return buildCollectionSample("conn-1", "ep-1", r, latencyMs, kCap);
}

}

TEST(CollectionSample, SuccessCarriesTheIdsStatusAndBody)
{
    const auto r = okResponse();
    const json s = build(r, 123);

    EXPECT_EQ(s.at("connector_oid"), "conn-1");
    EXPECT_EQ(s.at("endpoint_oid"), "ep-1");
    EXPECT_TRUE(s.at("ok").get<bool>());
    EXPECT_EQ(s.at("http_status").get<int>(), 200);
    EXPECT_EQ(s.at("latency_ms").get<long long>(), 123);
    EXPECT_EQ(s.at("bytes").get<std::size_t>(), r.body.size());
    EXPECT_FALSE(s.at("truncated").get<bool>());
    EXPECT_EQ(s.at("body"), r.body);
    // No failure, so no error key.
    EXPECT_FALSE(s.contains("error"));
}

TEST(CollectionSample, HttpErrorIsNotOkAndNamesTheStatus)
{
    auto r = okResponse();
    r.status = 403;
    r.body = "forbidden";
    const json s = build(r);

    EXPECT_FALSE(s.at("ok").get<bool>());
    EXPECT_EQ(s.at("http_status").get<int>(), 403);   // still present — the request was sent
    EXPECT_EQ(s.at("error"), "HTTP 403");
}

TEST(CollectionSample, Non200IsNotOkEvenAt201)
{
    auto r = okResponse();
    r.status = 201;
    EXPECT_FALSE(build(r).at("ok").get<bool>());
}

TEST(CollectionSample, TlsFailureOmitsStatusAndReportsTheError)
{
    ClientResponse r;
    r.tlsOk = false;
    r.requestSent = false;
    r.error = "connection refused";
    const json s = build(r);

    EXPECT_FALSE(s.at("ok").get<bool>());
    EXPECT_FALSE(s.contains("http_status"));   // never sent, so no status
    EXPECT_EQ(s.at("error"), "connection refused");
}

TEST(CollectionSample, TlsFailureWithoutDetailFallsBackToAGenericMessage)
{
    ClientResponse r;
    r.tlsOk = false;
    r.requestSent = false;
    const json s = build(r);

    EXPECT_EQ(s.at("error"), "TLS handshake failed");
}

TEST(CollectionSample, PinGateRefusalIsDistinctFromATlsFailure)
{
    // Handshake completed (tlsOk) but the pin did not match, so the request was withheld.
    ClientResponse r;
    r.tlsOk = true;
    r.pinMatched = false;
    r.requestSent = false;
    const json s = build(r);

    EXPECT_FALSE(s.at("ok").get<bool>());
    EXPECT_FALSE(s.contains("http_status"));
    EXPECT_EQ(s.at("error"), "certificate not trusted — pin the device first");
}

TEST(CollectionSample, OversizedBodyIsTruncatedButBytesReportsTheFullSize)
{
    auto r = okResponse();
    r.body = std::string(kCap + 55, 'x');   // 55 over the cap
    const json s = build(r);

    EXPECT_TRUE(s.at("truncated").get<bool>());
    EXPECT_EQ(s.at("body").get<std::string>().size(), kCap);       // stored body is capped
    EXPECT_EQ(s.at("bytes").get<std::size_t>(), r.body.size());    // but the true size is kept
    EXPECT_TRUE(s.at("ok").get<bool>());                           // truncation does not fail the sample
}

TEST(CollectionSample, BodyExactlyAtTheCapIsNotTruncated)
{
    auto r = okResponse();
    r.body = std::string(kCap, 'y');   // exactly the cap — the test is `>` not `>=`
    const json s = build(r);

    EXPECT_FALSE(s.at("truncated").get<bool>());
    EXPECT_EQ(s.at("body").get<std::string>().size(), kCap);
}
