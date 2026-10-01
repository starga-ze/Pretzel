#include "service/pbuserrequest/PbUserRequestService.h"

#include "config/Config.h"
#include "db/Database.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pz::engined
{

using json = nlohmann::json;

namespace
{

// The endpoint subtype this service answers to, spelled the way the console and collectord spell
// it. An endpoint of any other product is not ours and is not parsed.
constexpr const char* kSubtype = "pab";

const json& connectorSection()
{
    return pz::config::Config::section(pz::config::scope::kPretzel, "connector");
}

json findByOid(const json& arr, const std::string& oid)
{
    for (const auto& x : arr)
        if (x.is_object() && x.value("oid", std::string()) == oid)
            return x;
    return json::object();
}

// Which site a connector collects for. The connector names a device, and the device names the site;
// pb_user_request carries it so the console can scope the queue the way it scopes everything else.
std::string siteOfDevice(const std::string& deviceOid)
{
    const auto& site = pz::config::Config::section(pz::config::scope::kPretzel, "site");
    for (const char* key : {"ngfw_devices", "sase_devices"})
        for (const auto& d : site.value(key, json::array()))
            if (d.is_object() && d.value("oid", std::string()) == deviceOid)
                return d.value("site", std::string());
    return {};
}

// A JSON value as a SQL text parameter: strings as themselves, anything else as its JSON form, and
// a missing field as empty so NULLIF can turn it back into NULL.
std::string textOf(const json& o, const char* key)
{
    if (!o.is_object() || !o.contains(key) || o[key].is_null())
        return {};
    const auto& v = o[key];
    return v.is_string() ? v.get<std::string>() : v.dump();
}

}

void PbUserRequestService::onCollectionSample(const std::string& connectorOid,
                                              const std::string& endpointOid, const std::string& body)
{
    if (body.empty())
        return;

    const auto& conn = connectorSection();
    const json endpoint = findByOid(conn.value("endpoints", json::array()), endpointOid);
    if (endpoint.value("subtype", std::string()) != kSubtype)
        return;   // Not a Prisma Browser endpoint — the overwhelmingly common case, and free.

    const json doc = json::parse(body, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("data") || !doc["data"].is_array())
    {
        LOG_WARN("pb user-request sample is not the expected list document (connector={})", connectorOid);
        return;
    }

    const json connector = findByOid(conn.value("connectors", json::array()), connectorOid);
    const std::string siteOid = siteOfDevice(connector.value("object", std::string()));

    std::size_t stored = 0;

    for (const auto& row : doc["data"])
    {
        if (!row.is_object())
            continue;
        const std::string id = row.value("id", std::string());
        if (id.empty())
            continue;

        // Upsert, not insert: a request seen in ten consecutive polls is one row that changes. The
        // list document is replaced wholesale because it IS the newest truth about this request;
        // detail_json is deliberately left alone, so a detail read that has already landed is not
        // thrown away by the next poll of the list.
        const bool wrote = pz::db::Database::instance().exec(
            "INSERT INTO pb_user_request "
            "(id, site_oid, connector_oid, endpoint_oid, type, status, created_at, response_time, list_json) "
            "VALUES ($1,$2,$3,$4,NULLIF($5,''),NULLIF($6,''),NULLIF($7,'')::timestamptz,"
            "        NULLIF($8,'')::timestamptz,$9::jsonb) "
            "ON CONFLICT (id) DO UPDATE SET "
            "  site_oid=EXCLUDED.site_oid, connector_oid=EXCLUDED.connector_oid, "
            "  endpoint_oid=EXCLUDED.endpoint_oid, type=EXCLUDED.type, status=EXCLUDED.status, "
            "  created_at=EXCLUDED.created_at, response_time=EXCLUDED.response_time, "
            "  list_json=EXCLUDED.list_json, updated_at=now()",
            {id, siteOid, connectorOid, endpointOid, textOf(row, "type"), textOf(row, "status"),
             textOf(row, "createdAt"), textOf(row, "responseTime"), row.dump()});

        if (!wrote)
        {
            LOG_WARN("pb_user_request write failed (id={})", id);
            continue;
        }
        ++stored;
    }

    LOG_INFO("pb user-requests projected (connector={}, rows={})", connectorOid, stored);
}

}
