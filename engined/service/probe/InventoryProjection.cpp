#include "service/probe/InventoryProjection.h"

namespace pz::engined::inventory
{

namespace
{

using nlohmann::json;

// The identity a device is addressed by. Three spellings are accepted because the field has been
// called all three across the configuration's history and an old saved configuration still has to
// project — reading only "oid" would drop those devices out of the inventory on the next reload.
std::string oidOf(const json& d)
{
    return d.value("oid", d.value("uuid", d.value("id", std::string())));
}

std::string field(const json& d, const char* key)
{
    return d.value(key, std::string());
}

// The health probe body is operator-authored and may be typed as JSON or as raw text. Either is
// stored verbatim; a JSON object is serialised rather than rejected.
std::string healthBodyOf(const json& health)
{
    if (!health.contains("body"))
        return {};
    const json& body = health["body"];
    return body.is_string() ? body.get<std::string>() : body.dump();
}

const json& arrayAt(const json& site, const char* key, const json& fallback)
{
    if (!site.is_object() || !site.contains(key) || !site[key].is_array())
        return fallback;
    return site[key];
}

}

Projection projectSite(const json& site)
{
    static const json kEmpty = json::array();

    Projection out;

    for (const auto& d : arrayAt(site, "ngfw_devices", kEmpty))
    {
        if (!d.is_object())
            continue;

        NgfwRow row;
        row.oid = oidOf(d);
        if (row.oid.empty())
            continue;

        row.site = field(d, "site");
        row.target = field(d, "target");
        row.name = field(d, "name");
        row.description = field(d, "description");
        row.fingerprint = field(d, "fingerprint");

        out.ngfw.push_back(std::move(row));
    }

    for (const auto& d : arrayAt(site, "sase_devices", kEmpty))
    {
        if (!d.is_object())
            continue;

        SaseRow row;
        row.oid = oidOf(d);
        if (row.oid.empty())
            continue;

        row.site = field(d, "site");
        row.target = field(d, "target");
        row.name = field(d, "name");
        row.description = field(d, "description");

        const json health = d.value("health", json::object());
        if (health.is_object())
        {
            row.healthUrl = field(health, "url");
            row.healthBody = healthBodyOf(health);
        }

        out.sase.push_back(std::move(row));
    }

    return out;
}

json oidsOf(const std::vector<NgfwRow>& rows)
{
    json ids = json::array();
    for (const auto& r : rows)
        ids.push_back(r.oid);
    return ids;
}

json oidsOf(const std::vector<SaseRow>& rows)
{
    json ids = json::array();
    for (const auto& r : rows)
        ids.push_back(r.oid);
    return ids;
}

}
