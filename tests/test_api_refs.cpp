// Covers pz::config::checkApiReferences — referential integrity inside scand.service.api.
//
// A connector is almost entirely references: which object, which credential, which endpoint.
// Endpoints are versioned by the vendor, so an estate accumulates several and connectors get
// re-pointed between them; the failure this guards against is deleting the one still in use and
// finding out weeks later that a collection quietly stopped.
//
// The rule must hold when a commit carries only ONE of the three arrays, which is the normal
// case — the endpoints page publishes just `endpoints`. Callers assemble the effective section
// first; these tests feed that assembled shape directly.

#include "config/ApiRefs.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

using nlohmann::json;
using pz::config::checkApiReferences;

namespace
{

json apiSection()
{
    return json{
        {"api_keys", json::array({{{"oid", "key-1"}, {"name", "core-fw creds"}}})},
        {"endpoints", json::array({{{"oid", "ep-v10"}, {"name", "Addresses v10.2"},
                                    {"path", "/restapi/v10.2/Objects/Addresses"}}})},
        {"connectors", json::array({{{"oid", "conn-1"},
                                     {"name", "core-fw addresses"},
                                     {"object", "dev-1"},
                                     {"auth_profile", "key-1"},
                                     {"items", json::array({{{"endpoint", "ep-v10"},
                                                             {"poll_interval_sec", 60}}})}}})},
    };
}

}

// ── The wired-up case ───────────────────────────────────────────────────────────────────

TEST(ApiRefs, AcceptsASectionWhereEveryReferenceResolves)
{
    std::string error = "untouched";
    EXPECT_TRUE(checkApiReferences(apiSection(), error));
    EXPECT_EQ("untouched", error) << "error must not be written on success";
}

TEST(ApiRefs, AcceptsASectionWithNoConnectors)
{
    json api = apiSection();
    api["connectors"] = json::array();

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error));
}

TEST(ApiRefs, AcceptsAnEmptyOrMalformedSectionRatherThanCrashing)
{
    // Reached during seeding, before anything has been published.
    std::string error;
    EXPECT_TRUE(checkApiReferences(json::object(), error));
    EXPECT_TRUE(checkApiReferences(json::array(), error));
    EXPECT_TRUE(checkApiReferences(json("not an object"), error));
}

// ── Deleting something still in use ─────────────────────────────────────────────────────

TEST(ApiRefs, RejectsDeletingAnEndpointAConnectorStillUses)
{
    // What the endpoints page produces when the operator removes the only endpoint.
    json api = apiSection();
    api["endpoints"] = json::array();

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("endpoint")) << error;
}

TEST(ApiRefs, RejectsDeletingAnApiKeyAConnectorStillUses)
{
    json api = apiSection();
    api["api_keys"] = json::array();

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("API key")) << error;
}

TEST(ApiRefs, NamesTheConnectorThatIsBlocking)
{
    // Without the name the refusal is unactionable: the operator is on the endpoints page and
    // cannot see which connector objected.
    json api = apiSection();
    api["endpoints"] = json::array();

    std::string error;
    ASSERT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("core-fw addresses")) << error;
}

TEST(ApiRefs, FallsBackToTheOidWhenTheConnectorHasNoName)
{
    json api = apiSection();
    api["endpoints"] = json::array();
    api["connectors"][0].erase("name");

    std::string error;
    ASSERT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("conn-1")) << error;
}

TEST(ApiRefs, SaysWhatToDoAboutIt)
{
    json api = apiSection();
    api["endpoints"] = json::array();

    std::string error;
    ASSERT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("remove that entry")) << error;
}

// ── Adding something that points nowhere ────────────────────────────────────────────────

TEST(ApiRefs, RejectsAConnectorPointingAtAnUnknownEndpoint)
{
    json api = apiSection();
    api["connectors"][0]["items"][0]["endpoint"] = "ep-does-not-exist";

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
}

TEST(ApiRefs, RejectsAConnectorPointingAtAnUnknownApiKey)
{
    json api = apiSection();
    api["connectors"][0]["auth_profile"] = "key-does-not-exist";

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
}

// ── The upgrade this design exists for ──────────────────────────────────────────────────

TEST(ApiRefs, AcceptsTwoEndpointVersionsCoexisting)
{
    // A mixed estate: some firewalls on 10.2, some moved to 11.0. Both endpoints stay published
    // and connectors point at whichever applies — that is the whole point of the reference.
    json api = apiSection();
    api["endpoints"].push_back({{"oid", "ep-v11"},
                                {"name", "Addresses v11.0"},
                                {"path", "/restapi/v11.0/Objects/Addresses"}});
    api["connectors"].push_back({{"oid", "conn-2"},
                                 {"name", "edge-fw addresses"},
                                 {"object", "dev-2"},
                                 {"auth_profile", "key-1"},
                                 {"items", json::array({{{"endpoint", "ep-v11"}}})}});

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error)) << error;
}

TEST(ApiRefs, AcceptsRePointingAConnectorToTheNewVersion)
{
    json api = apiSection();
    api["endpoints"].push_back({{"oid", "ep-v11"},
                                {"name", "Addresses v11.0"},
                                {"path", "/restapi/v11.0/Objects/Addresses"}});
    api["connectors"][0]["items"][0]["endpoint"] = "ep-v11";

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error)) << error;
}

TEST(ApiRefs, RejectsRetiringTheOldVersionWhileAConnectorStillUsesIt)
{
    // The half-finished upgrade: the new endpoint was published and one connector moved, but
    // the old endpoint is being deleted while another connector is still on it.
    json api = apiSection();
    api["endpoints"] = json::array({{{"oid", "ep-v11"},
                                     {"name", "Addresses v11.0"},
                                     {"path", "/restapi/v11.0/Objects/Addresses"}}});
    api["connectors"].push_back({{"oid", "conn-2"},
                                 {"name", "edge-fw addresses"},
                                 {"object", "dev-2"},
                                 {"auth_profile", "key-1"},
                                 {"items", json::array({{{"endpoint", "ep-v11"}}})}});

    std::string error;
    ASSERT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("core-fw addresses"))
        << "the connector still on the old endpoint should be the one named: " << error;
}

// ── A connector is a schedule over several endpoints ────────────────────────────────────

TEST(ApiRefs, AcceptsAConnectorCollectingSeveralEndpoints)
{
    // The normal shape: one device, one credential, several things pulled at their own rates.
    json api = apiSection();
    api["endpoints"].push_back({{"oid", "ep-sysinfo"}, {"name", "System info"}, {"path", "/api/"}});
    api["connectors"][0]["items"] = json::array({
        {{"endpoint", "ep-v10"}, {"poll_interval_sec", 300}},
        {{"endpoint", "ep-sysinfo"}, {"poll_interval_sec", 60}},
    });

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error)) << error;
}

TEST(ApiRefs, RejectsWhenOnlyOneOfSeveralItemsDangles)
{
    // The realistic deletion: a device collects four things and one endpoint is being removed.
    json api = apiSection();
    api["endpoints"].push_back({{"oid", "ep-sysinfo"}, {"name", "System info"}, {"path", "/api/"}});
    api["connectors"][0]["items"] = json::array({
        {{"endpoint", "ep-v10"}},
        {{"endpoint", "ep-gone"}},
        {{"endpoint", "ep-sysinfo"}},
    });

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
    EXPECT_NE(std::string::npos, error.find("core-fw addresses")) << error;
}

TEST(ApiRefs, AcceptsAConnectorThatCollectsNothingYet)
{
    // An empty schedule is useless but not inconsistent; scand warns and collects nothing.
    json api = apiSection();
    api["connectors"][0]["items"] = json::array();

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error)) << error;
}

TEST(ApiRefs, DisabledItemsAreStillCheckedForDanglingReferences)
{
    // A disabled entry is one the operator intends to re-enable; letting its endpoint be deleted
    // would turn re-enabling it into a silent no-op later.
    json api = apiSection();
    api["connectors"][0]["items"] = json::array({{{"endpoint", "ep-gone"}, {"enabled", false}}});

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
}

// ── Shapes that must not be mistaken for a valid reference ──────────────────────────────

TEST(ApiRefs, IgnoresAConnectorWithNoReferencesSet)
{
    // A half-filled draft is the editor's problem, not this rule's; the field validators reject
    // it separately. This must not report a dangling reference for an absent one.
    json api = apiSection();
    api["connectors"] = json::array({{{"oid", "conn-blank"}, {"name", "draft"}}});

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error)) << error;
}

TEST(ApiRefs, SkipsNonObjectEntries)
{
    json api = apiSection();
    api["connectors"].push_back("garbage");
    api["connectors"].push_back(42);

    std::string error;
    EXPECT_TRUE(checkApiReferences(api, error)) << error;
}

TEST(ApiRefs, DoesNotMatchOnNameOrPathInsteadOfOid)
{
    // A reference resolves by oid only. Matching a display name would let a rename break the
    // link silently, which is exactly what oids exist to prevent.
    json api = apiSection();
    api["connectors"][0]["items"][0]["endpoint"] = "Addresses v10.2";   // the name, not the oid

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
}

TEST(ApiRefs, TreatsAMissingArrayAsEmptyRatherThanAsAWildcard)
{
    // If `endpoints` is absent entirely, a connector referencing one is still dangling.
    json api = apiSection();
    api.erase("endpoints");

    std::string error;
    EXPECT_FALSE(checkApiReferences(api, error));
}
