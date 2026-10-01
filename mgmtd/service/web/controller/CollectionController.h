#pragma once

#include "http/HttpMessage.h"

namespace pz::mgmtd
{

class MgmtdServiceManager;

// Read side of the API collection pipeline — what Insight ▸ API Collection draws.
//
// The page's subject is a STREAM: one connector's one endpoint, polled on its own interval. That is
// the only unit every API in the estate shares, because the payloads do not share anything — a
// hundred endpoints are a hundred different JSON schemas, so nothing above the stream level can be
// tabulated across them. Health, cadence and freshness are therefore reported per stream here, and
// the payload itself is only ever fetched one sample at a time (sample()), where the page can shape
// it to whatever that one API happens to return.
//
// A stream exists in two places and both are needed to tell the truth about it:
//
//   config — the operator's declaration (pretzel.connector.connectors[].items[]): this endpoint SHOULD
//            be polled every N seconds. A stream declared minutes ago with no samples yet is not an
//            error, and a stream that vanished from config is not a live stream however many rows it
//            left behind.
//   db     — api_collection: what actually came back, and when.
//
// overview() joins them so the page can distinguish "never ran" from "running" from "stopped
// running" — which is the question an operator actually has after adding an API connector.
class CollectionController
{
public:
    CollectionController() = default;

    // GET /api/collection/overview?window=<hours>
    // Every declared stream with its config, last outcome, window aggregate and a short spark
    // series. Never carries response bodies — this answer is drawn as a whole page.
    void overview(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/collection/samples?connector=&endpoint=&status=&before=&limit=
    // One stream's history, newest first, keyset-paginated on oid. Metadata only, no bodies.
    void samples(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/collection/sample?oid=<id>
    // One sample WITH its raw response body — the only route that returns a payload, so the size of
    // an answer is always one body and never a page of them.
    void sample(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/collection/run-now — {connector, endpoint}
    //
    // Brings one stream's next poll forward. Answers 202 and nothing else: the result of the poll
    // arrives the way every poll's does — as a sample engined stores — so the caller learns it by
    // re-reading, not by waiting here. A ticket would promise a correlation this has no way to keep.
    void runNow(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/collection/endpoint-status
    //
    // The newest sample per endpoint, across every connector that collects it. Exists because the
    // API Endpoint page's Status column was reading a per-browser-tab record of whether someone had
    // pressed Endpoint Test — so an endpoint collected successfully every minute for a week read
    // "never tested" in a fresh tab, and a colleague's test was invisible.
    //
    // Not scoped to a site, unlike overview: that page lists every site's endpoints at once, and
    // what it needs is one fact per endpoint rather than a site's streams.
    void endpointStatus(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);
};

}
