#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Read side of the security advisory pipeline — what Knowledge ▸ Security Advisories draws.
//
// collectord walks the vendor's published feed and engined writes it; this only reads. Straight
// database reads, not the ticket-and-poll the tech-doc routes next door use: that corpus lives in
// pretzel-ai behind gRPC and has to be asked for, while these rows are in engined's own database,
// which mgmtd reads directly (the same arrangement CollectionController has for api_collection).
//
// The page is a QUEUE, not a catalogue, and the queries are shaped by that. `list` filters on
// severity, product, recency and text, and orders by activity — publication or revision, whichever
// is later — because "what moved" is the question being asked. `overview` is a single pass for the
// severity distribution the page leads with, kept apart from the list so paging never re-counts
// the estate.
class AdvisoryController
{
public:
    AdvisoryController() = default;

    // GET /api/advisory/overview
    // The severity distribution, the revised-recently count and the last collection's outcome.
    void overview(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/advisory/list?severity=&product=&q=&window=&sort=&offset=&limit=
    // One page of advisories, plus the total and the whole product vocabulary for the filter.
    void list(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/advisory/detail?id=<advisory id>
    // One advisory in full, including the CVSS vector and the affected/fixed version table, which
    // are read out of the stored feed record rather than promoted to columns nothing filters on.
    void detail(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/advisory/refresh
    // Collect now, ahead of the interval. Fire and forget: collectord answers by writing rows, and
    // the console learns the result by re-reading its own (see IpcCmd::AdvisoryRefreshRequest).
    void refresh(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
