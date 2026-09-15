#pragma once

#include "http/HttpMessage.h"

#include <string>

namespace pz::apid
{

// The token an Authorization header carries, or "" when the header is absent, malformed, or uses
// any scheme other than Bearer. Returning "" rather than the raw header is what lets the caller
// compare against a configured token without accidentally matching a prefix.
std::string bearerToken(const std::string& authorization);

// apid's entire HTTP surface, as a pure decision: (request, expected token) in, response out. It
// is split out of IngestService because everything else in that class is plumbing — the service
// manager, the action queue, the Tx router — while this is the part that decides what the world
// gets back, and the part worth checking without standing up a daemon.
//
// `resp` is left untouched for a request that matches no route, so the caller's default (404) is
// the answer.
void routeIngest(const pz::http::HttpRequest& req, const std::string& ingestToken, pz::http::HttpResponse& resp);

}
