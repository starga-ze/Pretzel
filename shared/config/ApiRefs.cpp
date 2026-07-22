#include "config/ApiRefs.h"

#include <nlohmann/json.hpp>

namespace pz::config
{

namespace
{

using json = nlohmann::json;

bool containsOid(const json& arr, const std::string& oid)
{
    if (!arr.is_array())
        return false;

    for (const auto& entry : arr)
    {
        if (entry.is_object() && entry.value("oid", std::string()) == oid)
            return true;
    }
    return false;
}

}

bool checkApiReferences(const json& api, std::string& error)
{
    if (!api.is_object())
        return true;

    const json endpoints = api.value("endpoints", json::array());
    const json apiKeys = api.value("api_keys", json::array());
    const json connectors = api.value("connectors", json::array());

    if (!connectors.is_array())
        return true;

    for (const auto& c : connectors)
    {
        if (!c.is_object())
            continue;

        // Named, not just identified: an operator deleting an endpoint needs to know which
        // connector is blocking them, or the refusal is unactionable.
        const std::string label = c.value("name", c.value("oid", std::string("unnamed")));

        // A connector collects several endpoints, each on its own schedule; every one of them
        // is a reference that has to resolve.
        const json items = c.value("items", json::array());
        if (items.is_array())
        {
            for (const auto& item : items)
            {
                if (!item.is_object())
                    continue;

                const std::string endpoint = item.value("endpoint", std::string());
                if (!endpoint.empty() && !containsOid(endpoints, endpoint))
                {
                    error = "connector '" + label +
                            "' collects an endpoint that does not exist — remove that entry from the "
                            "connector first";
                    return false;
                }
            }
        }

        const std::string authProfile = c.value("auth_profile", std::string());
        if (!authProfile.empty() && !containsOid(apiKeys, authProfile))
        {
            error = "connector '" + label +
                    "' references an API key that does not exist — re-point or remove that connector first";
            return false;
        }
    }

    return true;
}

}
