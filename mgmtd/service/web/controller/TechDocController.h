#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// System Management ▸ Operation ▸ Tech Documentation, the server half of it.
//
// The assistant answers out of a corpus crawled from docs.paloaltonetworks.com, and Palo Alto
// republishes those docs continuously, so the console has to be able to bring it current. mgmtd
// owns none of that work: it relays to pretzel-ai, which is the process that holds the crawler
// and is the sole writer of the pretzel_knowledge database.
//
// Two shapes, because the two operations are not alike:
//
//   check / status  are seconds, and use the same ticket-and-poll path as a chat turn — dispatch
//                   returns a ticket immediately, the answer is filed under it, the page polls.
//
//   refresh         runs for minutes, and the operator wants the progress *so far* on every poll
//                   rather than one answer at the end. It has no ticket: there is at most one
//                   refresh on the appliance at a time, so /progress reads the single live slot.
//                   That is also what lets the card recover its progress bar after a page reload.
class TechDocController
{
public:
    // GET  /api/techdoc/status            → 202 {ticket, status:"pending"}
    void status(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/techdoc/check {scope}     → 202 {ticket, status:"pending"}
    void check(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET  /api/techdoc/result?ticket=<n> → {status:"pending"} until pretzel-ai answers.
    // Shared by status and check: both resolve to one JSON document under a ticket.
    void result(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/techdoc/refresh {scope}   → 202 {started:true}, or 409 when one is already running
    void refresh(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET  /api/techdoc/progress          → the latest progress message, plus running:true|false
    void progress(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/techdoc/cancel            → 202; stops the crawl on the appliance, not just here
    void cancel(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
