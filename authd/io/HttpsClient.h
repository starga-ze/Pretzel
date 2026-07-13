#pragma once

#include <cstdint>
#include <string>

namespace pz::net
{

// Minimal synchronous HTTPS client built on Boost.Beast + OpenSSL (both already
// vendored). Blocking by design — authd calls it inline from its loop for the short
// Okta /token and /keys round-trips.
//
// NOTE (dedupe): this is a namespace-renamed copy of scand/api/HttpsClient. When time
// permits, promote a single copy to shared/ (pz-shared would then need Boost + OpenSSL)
// and delete both per-daemon copies.
class HttpsClient
{
public:
    struct Response
    {
        bool        ok{false};   // transport succeeded AND status is 2xx
        int         status{0};
        std::string body;
        std::string error;       // populated on transport/TLS failure
    };

    struct Request
    {
        std::string method{"GET"};   // "GET" | "POST"
        std::string host;
        uint16_t    port{443};
        std::string target{"/"};     // path + query, already URL-encoded
        std::string body;            // request body (POST)
        std::string contentType{"application/x-www-form-urlencoded"};
        int         timeoutMs{5000};
        bool        verifyTls{true}; // default ON for Okta (public CA)
    };

    static Response send(const Request& req);

    static Response get(const std::string& host, uint16_t port,
                        const std::string& target, int timeoutMs, bool verifyTls);
    static Response postForm(const std::string& host, uint16_t port,
                             const std::string& target, const std::string& formBody,
                             int timeoutMs, bool verifyTls);

    static std::string urlEncode(const std::string& s);
};

} // namespace pz::net
