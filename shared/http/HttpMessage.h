#pragma once

#include <cstdint>
#include <string>

namespace pz::http
{

using SessionId = std::uint64_t;

struct HttpRequest
{
    std::string method;
    std::string target;
    std::string authorization;
    std::string cookie;
    std::string body;
};

struct HttpResponse
{
    int status{404};
    std::string contentType{"application/json; charset=utf-8"};
    std::string body{R"({"error":"not found"})"};

    std::string setCookie;
    std::string location;
    std::string contentDisposition;
    std::string etag;
};

}
