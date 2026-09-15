#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace pz::mgmtd::settings
{

// What the settings API will accept, separated from the endpoint that accepts it.
//
// The commit endpoint is generic — any domain of a known scope passes through — but the domains
// the UI publishes carry a declared schema, and this is where a malformed entry is stopped before
// it can reach running_config. running_config is append-versioned and revertable, so a bad entry
// written there is not a transient error: it mints a permanent version, shows up verbatim in every
// later review diff, and is reloaded by eight daemons.
//
// It lives apart from SettingsController because the judgement and the plumbing answer different
// questions. The controller's job is HTTP, IPC and the commit queue; these functions decide only
// whether a document is well-formed, which is a pure question over JSON and is checked as one.

// The scopes a commit may address, and the ones GET /api/settings projects for the editors.
extern const char* const kSettingsScopes[2];
extern const char* const kHiddenDomains[2];

// Shape only: every entry in every array this domain owns is well-formed on its own. Deliberately
// says nothing about references — those cannot be judged one change at a time.
//
// An unknown (scope, domain) is accepted: the endpoint is generic by design, and only the domains
// the UI publishes carry a declared schema.
bool validateCommitShape(const std::string& scopeName, const std::string& domain, const nlohmann::json& values,
                         std::string& error);

// Referential integrity, run once per (scope, domain) over the MERGED values of every change in the
// batch that targets it — never per change, because the three API editors each publish their own
// slice of pretzel.connector and one operator action routinely arrives as several sibling entries.
bool validateCommitRefs(const std::string& scopeName, const std::string& domain, const nlohmann::json& values,
                        std::string& error);

}
