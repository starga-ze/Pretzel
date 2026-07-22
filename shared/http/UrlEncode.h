#pragma once

#include <string>

namespace pz::http
{

// Percent-encode one query-string value. PAN-OS takes credentials and XML API commands as query
// parameters, and mgmtd reuses this to render the request line it logs — so it lives in shared,
// independent of the outbound client (which is scand's alone).
std::string urlEncode(const std::string& raw);

}
