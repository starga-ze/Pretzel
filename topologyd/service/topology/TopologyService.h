#pragma once

#include "service/topology/TopologyEvent.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pz::topologyd
{

class TopologydServiceManager;

// Composes one site's infrastructure picture and answers mgmtd with it.
//
// Why this is a daemon and not a function in mgmtd: composition is the whole job. Three unrelated
// vendor documents arrive through the collector — a Prisma egress-IP inventory, a ZTNA connector
// list, a firewall's interfaces and tunnels — and none of them is a topology on its own. Turning
// them into one picture is where the domain knowledge lives, and it is the part that will keep
// growing as more APIs land. mgmtd stays a web server: it asks, it serves what came back.
//
// Nothing here talks to a device. Acquisition belongs to collectord, which holds the credentials and
// the pinned client; this reads what collectord already collected. That means the picture is only as
// fresh as the last collection cycle, which the answer states rather than hides.
//
// The site is the unit. Every device belongs to one, so a site scopes the devices, which scope the
// connectors, which scope the samples — that chain is how `site` becomes a key over a table that has
// no site column of its own.

// The newest sample per (connector, endpoint) stream — the raw material of a composition.
struct CollectedSample
{
    std::string body;
    std::string at;
    bool ok{false};
};
using SampleMap = std::unordered_map<std::string, CollectedSample>;

class TopologyService
{
public:
    TopologyService() = default;
    ~TopologyService() = default;

    void handleEvent(TopologydServiceManager& serviceManager, const TopologyEvent& event);

private:
    // Build the answer for one site ('' = every site).
    nlohmann::json compose(const std::string& siteOid);

    // The two halves of the picture. Separate because they are separate estates that happen to
    // belong to the same customer: the SASE fabric is a service someone else runs, the firewalls are
    // boxes in the customer's own racks. They are not two views of one graph.
    // `samples` is read once per composition and passed to both halves. It carries the response
    // bodies, so scanning it twice would double the only expensive part of this job.
    nlohmann::json composeSase(const std::string& siteOid, const SampleMap& samples, nlohmann::json& sources);
    nlohmann::json composeNgfw(const std::string& siteOid, const SampleMap& samples, nlohmann::json& sources);

    void reply(TopologydServiceManager& sm, std::uint32_t seqNo, const nlohmann::json& model);
};

}
