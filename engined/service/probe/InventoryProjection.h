#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace pz::engined::inventory
{

// What the OPERATOR declared, read out of the site configuration and shaped into the rows the
// device tables hold. Split out of ProbeService::projectInventory so the reading is separable from
// the writing: the SQL stays in the service, and the question "which rows should exist, with which
// values" — the part that decides whether a device silently disappears from the inventory — can be
// answered without a database.
//
// Only the config-projected columns appear here. Runtime state (status, last_seen, api_key_enc,
// egress_result) is written elsewhere and must survive a reload, which is why the upsert never
// touches it and why it has no place in this struct.

struct NgfwRow
{
    std::string oid;
    std::string site;
    std::string target;
    std::string name;
    std::string description;
    std::string fingerprint;
};

struct SaseRow
{
    std::string oid;
    std::string site;
    std::string target;
    std::string name;
    std::string description;
    std::string healthUrl;
    std::string healthBody;
};

struct Projection
{
    std::vector<NgfwRow> ngfw;
    std::vector<SaseRow> sase;
};

// Read one `site` configuration section. Entries that are not objects, and entries carrying no
// identity at all, are skipped rather than written as a blank row — a device with no oid cannot be
// addressed, updated or deleted later, so a row for it would be unreachable.
Projection projectSite(const nlohmann::json& site);

// The oid list the delete sweep compares against, as the JSON array the query takes.
//
// Note what an EMPTY list means: the sweep deletes every row, because the configuration declares no
// devices. That is correct — the tables are a projection of the configuration, not an accumulation
// — but it is also why `projectSite` must never return an empty list for a configuration it merely
// failed to understand.
nlohmann::json oidsOf(const std::vector<NgfwRow>& rows);
nlohmann::json oidsOf(const std::vector<SaseRow>& rows);

}
