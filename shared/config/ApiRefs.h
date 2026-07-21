#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace pz::config
{

// Referential integrity for a scand.service.api section.
//
// The section holds three arrays — api_keys, endpoints, connectors — and a connector is nothing
// but references: which object, which credential, which endpoint. Endpoints in particular are
// versioned by the vendor (/restapi/v10.2/…), so an estate accumulates several and connectors
// are re-pointed between them; deleting the one still in use has to be refused rather than
// discovered later as a collection that quietly stopped.
//
// Takes the EFFECTIVE section — what is stored, overlaid with what is being committed — because
// a commit usually carries only one array. `error` names the offending connector, since the
// operator hitting this is normally deleting something and needs to know what is holding it.
//
// Returns true when every reference resolves (and for a section with no connectors).
bool checkApiReferences(const nlohmann::json& api, std::string& error);

}
