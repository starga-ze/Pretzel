#pragma once

#include <cstdint>
#include <string>

namespace pz::scand
{

// Minimal synchronous HTTPS client built on Boost.Beast + OpenSSL (both already
// vendored — no new dependency). Blocking by design: it runs inside the SNMP
// engine's worker-pool threads, never on the main loop. Defaults to NOT verifying
// the peer certificate, since network appliances ship self-signed management certs.
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
        bool        verifyTls{false};
    };

    static Response send(const Request& req);

    // Convenience wrappers.
    static Response get(const std::string& host, uint16_t port,
                        const std::string& target, int timeoutMs, bool verifyTls);
    static Response postForm(const std::string& host, uint16_t port,
                             const std::string& target, const std::string& formBody,
                             int timeoutMs, bool verifyTls);

    // Percent-encode a string for use in a query value or form body.
    static std::string urlEncode(const std::string& s);
};

} // namespace pz::scand
