#pragma once

#include <cstdint>
#include <string>

namespace pz::scand
{

class HttpsClient
{
public:
    struct Response
    {
        bool ok{false};
        int status{0};
        std::string body;
        std::string error;
    };

    struct Request
    {
        std::string method{"GET"};
        std::string host;
        uint16_t port{443};
        std::string target{"/"};
        std::string body;
        std::string contentType{"application/x-www-form-urlencoded"};
        int timeoutMs{5000};
        bool verifyTls{false};
    };

    static Response send(const Request& req);

    static Response get(const std::string& host, uint16_t port, const std::string& target, int timeoutMs,
                        bool verifyTls);
    static Response postForm(const std::string& host, uint16_t port, const std::string& target,
                             const std::string& formBody, int timeoutMs, bool verifyTls);

    static std::string urlEncode(const std::string& s);
};

}
