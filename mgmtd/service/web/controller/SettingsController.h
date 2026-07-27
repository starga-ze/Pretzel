#pragma once

namespace pz::mgmtd
{

class WebRouter;

// Configuration surface: GET /api/settings, GET /api/settings/running-config,
// POST /api/settings/commit, GET /api/settings/reload-status, GET /api/settings/commit-queue.
// Reads the active running-config for the editors and validates a commit's per-domain schema before
// forwarding it to engined. One domain, one file: the handlers and the commit schema are private to
// the .cpp; only route registration is exposed.
class SettingsController
{
public:
    static void registerRoutes(WebRouter& router);
};

}
