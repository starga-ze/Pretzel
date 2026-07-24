#pragma once

#include "http/HttpMessage.h"

#include <string>
#include <utility>

namespace pz::mgmtd
{

// Fill an HTTP response in one line — the small shape every web handler ends on. Inline in a header
// so each controller shares the one definition rather than repeating it.
inline void fill(pz::http::HttpResponse& r, int status, std::string body,
                 std::string contentType = "application/json; charset=utf-8")
{
    r.status = status;
    r.contentType = std::move(contentType);
    r.body = std::move(body);
}

}
