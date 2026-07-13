#pragma once

#include <string>

namespace pz::http
{

// Transport-agnostic HTTP request/response DTOs. The Handler layer translates between
// boost::beast messages and these; everything past the Handler (RxRouter, events,
// services) works on the DTOs and never sees beast — so HTTP requests flow through the
// same event/action machinery as IPC, and services stay transport-agnostic.
struct HttpRequestView
{
    std::string method;         // "GET", "POST", ...
    std::string target;         // "/api/probe/egress"
    std::string authorization;  // raw "Authorization" header value ("" if absent)
    std::string body;           // raw request body
};

struct HttpResponse
{
    int         status{404};
    std::string contentType{"application/json; charset=utf-8"};
    std::string body{R"({"error":"not found"})"};
};

} // namespace pz::http
