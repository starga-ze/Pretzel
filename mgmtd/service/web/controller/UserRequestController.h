#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Site ▸ Control ▸ User Requests — the read side of pb_user_request.
//
// Reads rows, not samples. The queue is projected out of the collected list document by engined, so
// by the time the console asks, the question is a plain SELECT scoped to one site — no body to
// parse, no sample to pick, and nothing lost when api_collection releases its payloads.
//
// Read-only, like every other controller that serves a projection. Acting on a request is a POST to
// the vendor and belongs on the connector-test path, where the daemon that owns outbound calls can
// make it.
class UserRequestController
{
public:
    // GET /api/user-requests?site=<oid>[&status=<s>][&limit=<n>]
    void list(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/user-requests/action — {id, action: approve|decline|revoke, comment?, bypass?}
    //
    // The one place that knows a Prisma Browser request is acted on at "{list path}/{id}/action"
    // and withdrawn at "/{id}/revoke". collectord is handed the finished path and a body; it makes
    // the call and learns nothing about what it meant.
    //
    // Answers 202 with a ticket, polled on /api/connector/test-result like every other connector
    // operation. The vendor's own refusal travels back verbatim — "already acted on" and "not
    // found" are different problems and only the body tells them apart.
    void action(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
