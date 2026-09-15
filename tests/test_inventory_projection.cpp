// engined's device tables are a projection of the operator's configuration, not an accumulation of
// what has ever been seen. Every reload rewrites them: what the configuration declares is upserted,
// and everything else is swept away by a delete keyed on the oid list this projection returns.
//
// That makes two failure modes silent and expensive. A device the projection fails to read simply
// vanishes from the inventory on the next reload — no error, no log, just an empty row in the UI and
// a probe that stops running. And a value it reads wrongly is written over the good one, because the
// upsert always wins.
//
// The SQL stays in ProbeService; this is the part that decides what the SQL is handed.

#include "service/probe/InventoryProjection.h"

#include <gtest/gtest.h>

#include <string>

using namespace pz::engined::inventory;
using json = nlohmann::json;

namespace
{

json siteWithNgfw(json devices)
{
    return json{{"ngfw_devices", std::move(devices)}};
}

json siteWithSase(json devices)
{
    return json{{"sase_devices", std::move(devices)}};
}

}

// ── NGFW rows ───────────────────────────────────────────────────────────────────────────────────

TEST(InventoryProjection, CarriesEveryConfigProjectedNgfwColumn)
{
    const auto p = projectSite(siteWithNgfw(json::array({json{{"oid", "dev-1"},
                                                              {"site", "site-a"},
                                                              {"target", "10.0.0.1"},
                                                              {"name", "Sherpain-FW"},
                                                              {"description", "HQ perimeter"},
                                                              {"fingerprint", "AA:BB:CC"}}})));

    ASSERT_EQ(1u, p.ngfw.size());
    EXPECT_EQ("dev-1", p.ngfw[0].oid);
    EXPECT_EQ("site-a", p.ngfw[0].site);
    EXPECT_EQ("10.0.0.1", p.ngfw[0].target);
    EXPECT_EQ("Sherpain-FW", p.ngfw[0].name);
    EXPECT_EQ("HQ perimeter", p.ngfw[0].description);
    EXPECT_EQ("AA:BB:CC", p.ngfw[0].fingerprint);
}

TEST(InventoryProjection, MissingOptionalFieldsBecomeEmptyRatherThanDroppingTheDevice)
{
    // A device is created with a name and an address long before it has a pin or a site, and it
    // still has to appear in the inventory while that is true.
    const auto p = projectSite(siteWithNgfw(json::array({json{{"oid", "dev-1"}, {"name", "new-fw"}}})));

    ASSERT_EQ(1u, p.ngfw.size());
    EXPECT_EQ("new-fw", p.ngfw[0].name);
    EXPECT_EQ("", p.ngfw[0].target);
    EXPECT_EQ("", p.ngfw[0].fingerprint);
    EXPECT_EQ("", p.ngfw[0].site);
}

TEST(InventoryProjection, AcceptsTheOlderSpellingsOfTheIdentityField)
{
    // The field has been called all three across the configuration's history. A saved configuration
    // written under an older name must still project, or reloading it deletes those devices.
    const auto p = projectSite(siteWithNgfw(json::array({json{{"oid", "by-oid"}},
                                                         json{{"uuid", "by-uuid"}},
                                                         json{{"id", "by-id"}}})));

    ASSERT_EQ(3u, p.ngfw.size());
    EXPECT_EQ("by-oid", p.ngfw[0].oid);
    EXPECT_EQ("by-uuid", p.ngfw[1].oid);
    EXPECT_EQ("by-id", p.ngfw[2].oid);
}

TEST(InventoryProjection, PrefersOidWhenSeveralSpellingsArePresent)
{
    const auto p = projectSite(
        siteWithNgfw(json::array({json{{"oid", "canonical"}, {"uuid", "older"}, {"id", "oldest"}}})));

    ASSERT_EQ(1u, p.ngfw.size());
    EXPECT_EQ("canonical", p.ngfw[0].oid);
}

TEST(InventoryProjection, SkipsADeviceWithNoIdentityAtAll)
{
    // A row with no oid could never be updated or deleted afterwards — the upsert conflicts on oid
    // and the sweep compares on it. Writing one would leave an unreachable row behind forever.
    const auto p = projectSite(siteWithNgfw(json::array({json{{"name", "nameless"}},
                                                         json{{"oid", ""}, {"name", "blank oid"}},
                                                         json{{"oid", "real"}}})));

    ASSERT_EQ(1u, p.ngfw.size());
    EXPECT_EQ("real", p.ngfw[0].oid);
}

TEST(InventoryProjection, SkipsEntriesThatAreNotObjectsWithoutLosingTheRest)
{
    const auto p = projectSite(siteWithNgfw(json::array({"a string", 42, json::array({1}), nullptr,
                                                         json{{"oid", "real"}}})));

    ASSERT_EQ(1u, p.ngfw.size());
    EXPECT_EQ("real", p.ngfw[0].oid);
}

TEST(InventoryProjection, KeepsConfigurationOrder)
{
    const auto p = projectSite(
        siteWithNgfw(json::array({json{{"oid", "a"}}, json{{"oid", "b"}}, json{{"oid", "c"}}})));

    ASSERT_EQ(3u, p.ngfw.size());
    EXPECT_EQ("a", p.ngfw[0].oid);
    EXPECT_EQ("b", p.ngfw[1].oid);
    EXPECT_EQ("c", p.ngfw[2].oid);
}

// ── SASE rows ───────────────────────────────────────────────────────────────────────────────────

TEST(InventoryProjection, CarriesEveryConfigProjectedSaseColumn)
{
    const auto p = projectSite(siteWithSase(json::array({json{{"oid", "tenant-1"},
                                                              {"site", "site-a"},
                                                              {"target", "1963594622"},
                                                              {"name", "Prisma"},
                                                              {"description", "tenant"},
                                                              {"health", {{"url", "https://api.example/health"},
                                                                          {"body", "{\"q\":1}"}}}}})));

    ASSERT_EQ(1u, p.sase.size());
    EXPECT_EQ("tenant-1", p.sase[0].oid);
    EXPECT_EQ("1963594622", p.sase[0].target);
    EXPECT_EQ("https://api.example/health", p.sase[0].healthUrl);
    EXPECT_EQ(R"({"q":1})", p.sase[0].healthBody);
}

TEST(InventoryProjection, SerialisesAHealthBodyTypedAsJsonRatherThanRejectingIt)
{
    // The field is operator-authored and the UI lets it be written either way. A structured body
    // has to reach the probe as the text it will send.
    const auto p = projectSite(siteWithSase(json::array({json{
        {"oid", "t"}, {"health", {{"url", "https://x"}, {"body", {{"scope", "tsg"}, {"n", 2}}}}}}})));

    ASSERT_EQ(1u, p.sase.size());
    EXPECT_EQ(R"({"n":2,"scope":"tsg"})", p.sase[0].healthBody);
}

TEST(InventoryProjection, LeavesTheHealthProbeBlankWhenItIsNotConfigured)
{
    // A tenant with no health probe declared is still a device; it just has nothing to poll yet.
    for (const json device : {json{{"oid", "t"}},
                              json{{"oid", "t"}, {"health", json::object()}},
                              json{{"oid", "t"}, {"health", {{"url", "https://x"}}}}})
    {
        const auto p = projectSite(siteWithSase(json::array({device})));
        ASSERT_EQ(1u, p.sase.size()) << device.dump();
        EXPECT_EQ("", p.sase[0].healthBody) << device.dump();
    }
}

TEST(InventoryProjection, SurvivesAHealthFieldThatIsNotAnObject)
{
    // Malformed configuration must not take engined down on the reload path.
    for (const json health : {json("a string"), json(42), json::array({1}), json(nullptr)})
    {
        json device{{"oid", "t"}, {"name", "tenant"}};
        device["health"] = health;

        Projection p;
        ASSERT_NO_THROW(p = projectSite(siteWithSase(json::array({device})))) << health.dump();
        ASSERT_EQ(1u, p.sase.size()) << health.dump();
        EXPECT_EQ("", p.sase[0].healthUrl) << health.dump();
        EXPECT_EQ("", p.sase[0].healthBody) << health.dump();
    }
}

TEST(InventoryProjection, TheTwoDeviceKindsAreReadIndependently)
{
    // The array IS the type since the schema split, so an ngfw entry must never surface as a sase
    // row or the wrong table gets a device that has no business in it.
    json site;
    site["ngfw_devices"] = json::array({json{{"oid", "fw"}}});
    site["sase_devices"] = json::array({json{{"oid", "tenant"}}});

    const auto p = projectSite(site);
    ASSERT_EQ(1u, p.ngfw.size());
    ASSERT_EQ(1u, p.sase.size());
    EXPECT_EQ("fw", p.ngfw[0].oid);
    EXPECT_EQ("tenant", p.sase[0].oid);
}

// ── A configuration that says nothing ───────────────────────────────────────────────────────────

TEST(InventoryProjection, AnAbsentOrMalformedArrayYieldsNoRowsRatherThanThrowing)
{
    for (const json site : {json::object(),
                            json{{"ngfw_devices", json::array()}},
                            json{{"ngfw_devices", "not an array"}},
                            json{{"ngfw_devices", json::object()}},
                            json{{"ngfw_devices", nullptr}},
                            json("the whole section is a string"),
                            json(nullptr)})
    {
        Projection p;
        ASSERT_NO_THROW(p = projectSite(site)) << site.dump();
        EXPECT_TRUE(p.ngfw.empty()) << site.dump();
        EXPECT_TRUE(p.sase.empty()) << site.dump();
    }
}

// ── The oid list the delete sweep runs on ───────────────────────────────────────────────────────

TEST(InventorySweep, ListsTheOidOfEveryProjectedRowInOrder)
{
    const auto p = projectSite(
        siteWithNgfw(json::array({json{{"oid", "a"}}, json{{"oid", "b"}}, json{{"oid", "c"}}})));

    EXPECT_EQ(json::array({"a", "b", "c"}), oidsOf(p.ngfw));
}

TEST(InventorySweep, ListsSaseOidsSeparatelyFromNgfwOnes)
{
    json site;
    site["ngfw_devices"] = json::array({json{{"oid", "fw"}}});
    site["sase_devices"] = json::array({json{{"oid", "tenant"}}});

    const auto p = projectSite(site);

    // Each table is swept against its own list; crossing them would delete every row of both.
    EXPECT_EQ(json::array({"fw"}), oidsOf(p.ngfw));
    EXPECT_EQ(json::array({"tenant"}), oidsOf(p.sase));
}

TEST(InventorySweep, AnEmptyProjectionProducesAnEmptyJsonArrayNotNull)
{
    const auto p = projectSite(json::object());

    // The sweep interpolates this into `oid <> ALL(ARRAY(SELECT jsonb_array_elements_text($1)))`.
    // An empty array means "keep nothing", which is right for a configuration declaring no devices
    // — but it has to be a JSON array, because null would make the query fail rather than clear.
    EXPECT_TRUE(oidsOf(p.ngfw).is_array());
    EXPECT_TRUE(oidsOf(p.sase).is_array());
    EXPECT_EQ("[]", oidsOf(p.ngfw).dump());
}

TEST(InventorySweep, ExcludesTheDevicesTheProjectionSkipped)
{
    // The important consequence of skipping: a device with no oid is not merely absent from the
    // upsert, it is also absent from the keep-list, so any row it once had is swept.
    const auto p = projectSite(
        siteWithNgfw(json::array({json{{"oid", "keep"}}, json{{"name", "no identity"}}, "garbage"})));

    EXPECT_EQ(json::array({"keep"}), oidsOf(p.ngfw));
}
