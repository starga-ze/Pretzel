// topologyd's NGFW classifier: what a PAN-OS configuration document says a thing IS.
//
// The peer cases are taken verbatim from a live firewall's IKE gateway list, because the whole
// NGFW↔SASE picture rests on reading those FQDNs correctly and they are not a format anyone can
// re-derive from documentation. If Palo Alto changes the shape, these fail here rather than as a
// silently emptied lane on the page.

#include "service/topology/NgfwModel.h"

#include <gtest/gtest.h>

using namespace pz::topologyd::ngfw;
using json = nlohmann::json;

// ── Peer classification ───────────────────────────────────────────────────────────────────────

TEST(Peer, ServiceConnectionCarriesLocationRegionAndTenant)
{
    const Peer p = classifyPeer("sherpain-hq.south-korea.sc.osy5synss5.gpcloudservice.com");
    EXPECT_EQ(p.kind, PeerKind::ServiceConnection);
    EXPECT_EQ(p.label, "sherpain-hq");
    EXPECT_EQ(p.region, "south-korea");
    EXPECT_EQ(p.tenant, "osy5synss5");
}

TEST(Peer, RemoteNetworkFoldsRegionIntoItsLabel)
{
    // "south-korea-aspen.rn.<tenant>" — one label, not two. The region is not separately
    // recoverable here and must come back empty rather than be sliced out of the SPN name.
    const Peer p = classifyPeer("south-korea-aspen.rn.nnjy2ycyn.gpcloudservice.com");
    EXPECT_EQ(p.kind, PeerKind::RemoteNetwork);
    EXPECT_EQ(p.label, "south-korea-aspen");
    EXPECT_EQ(p.region, "");
    EXPECT_EQ(p.tenant, "nnjy2ycyn");
}

TEST(Peer, TenantTokenIsWhatSeparatesTwoPrismaTenants)
{
    // The join key for hub detection: same shape, different tenant, and the model must not merge
    // them into one fabric.
    const Peer a = classifyPeer("south-korea-aspen.rn.nnjy2ycyn.gpcloudservice.com");
    const Peer b = classifyPeer("south-korea-iberis.rn.s2yyn552n.gpcloudservice.com");
    EXPECT_EQ(a.kind, b.kind);
    EXPECT_NE(a.tenant, b.tenant);
}

TEST(Peer, MultiLabelLocationBeforeTheMarkerIsKept)
{
    const Peer p = classifyPeer("sc1.south-korea.sc.os2noocyjs.gpcloudservice.com");
    EXPECT_EQ(p.kind, PeerKind::ServiceConnection);
    EXPECT_EQ(p.label, "sc1");
    EXPECT_EQ(p.tenant, "os2noocyjs");
}

TEST(Peer, BareAddressIsAnExternalPeerNotAFabricEndpoint)
{
    for (const char* ip : {"128.77.92.93", "13.124.66.36", "3.35.244.78", "8.213.133.94"})
    {
        const Peer p = classifyPeer(ip);
        EXPECT_EQ(p.kind, PeerKind::External) << ip;
        EXPECT_EQ(p.tenant, "") << ip;
    }
}

TEST(Peer, HalfFinishedHostnameIsExternalRatherThanGuessedIntoTheFabric)
{
    // A real entry on the live box. It contains "sc-" and would match a sloppier rule; it is not a
    // Prisma endpoint and drawing it as one would put a fabricated link on the page.
    const Peer p = classifyPeer("sc-fqdn-eval");
    EXPECT_EQ(p.kind, PeerKind::External);
    EXPECT_EQ(p.tenant, "");
}

TEST(Peer, EmptyPeerIsUnknownNotExternal)
{
    const Peer p = classifyPeer("");
    EXPECT_EQ(p.kind, PeerKind::Unknown);
}

TEST(Peer, PortAndPrefixAreStrippedBeforeMatching)
{
    EXPECT_EQ(classifyPeer("1.214.192.59:12929").kind, PeerKind::External);
    EXPECT_EQ(classifyPeer("1.214.192.59/28").kind, PeerKind::External);
    EXPECT_EQ(classifyPeer("1.214.192.59/28").label, "1.214.192.59");
}

// ── Address scope ─────────────────────────────────────────────────────────────────────────────

TEST(Address, PrivateRangesCoverRfc1918CgnatAndLinkLocal)
{
    EXPECT_TRUE(isPrivateV4("192.168.1.1/23"));
    EXPECT_TRUE(isPrivateV4("172.16.118.1/24"));
    EXPECT_TRUE(isPrivateV4("10.0.0.1"));
    EXPECT_TRUE(isPrivateV4("100.64.0.1"));
    EXPECT_TRUE(isPrivateV4("169.254.1.1"));
    EXPECT_TRUE(isPrivateV4("127.0.0.1"));
}

TEST(Address, PublicAddressesFromTheLiveBoxAreNotPrivate)
{
    EXPECT_FALSE(isPrivateV4("1.214.192.59/28"));
    EXPECT_FALSE(isPrivateV4("1.220.4.222/29"));
    EXPECT_FALSE(isPrivateV4("172.15.0.1"));   // just outside 172.16/12
    EXPECT_FALSE(isPrivateV4("172.32.0.1"));   // just outside the other end
}

TEST(Address, NonAddressesAreNotClaimedAsPrivate)
{
    EXPECT_FALSE(isPrivateV4(""));
    EXPECT_FALSE(isPrivateV4("south-korea-aspen.rn.x.gpcloudservice.com"));
    EXPECT_FALSE(isPrivateV4("999.1.1.1"));
}

// ── Document readers ──────────────────────────────────────────────────────────────────────────

TEST(Entries, SingleEntryObjectIsNormalisedToAList)
{
    const json doc = json::parse(R"({"result":{"entry":{"@name":"only"}}})");
    const json e = entriesOf(doc);
    ASSERT_TRUE(e.is_array());
    ASSERT_EQ(e.size(), 1u);
    EXPECT_EQ(str(e[0], "@name"), "only");
}

TEST(Entries, AnEmptyPanOsResultYieldsNoEntriesRatherThanThrowing)
{
    // What TunnelInterfaces returns when the vsys scope holds none: success, count 0, no `entry`.
    const json doc = json::parse(R"({"@status":"success","@code":"7","result":{"@count":"0"}})");
    EXPECT_EQ(entriesOf(doc).size(), 0u);
}

TEST(Interfaces, AddressesComeFromTheEntryNameUnderTheModeNode)
{
    const json e = json::parse(R"({
      "@name":"ethernet1/1",
      "layer3":{"ip":{"entry":[{"@name":"192.168.1.1/23"},{"@name":"192.168.199.1/24"}]}}
    })");
    const auto addrs = interfaceAddresses(e);
    ASSERT_EQ(addrs.size(), 2u);
    EXPECT_EQ(addrs[0], "192.168.1.1/23");
    EXPECT_EQ(addrs[1], "192.168.199.1/24");
    EXPECT_EQ(interfaceMode(e), "layer3");
}

TEST(Interfaces, AdminStateReadsLinkStateThenDisabled)
{
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"down"})")), "down");
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"up"})")), "up");
    EXPECT_EQ(adminState(json::parse(R"({"disabled":"yes"})")), "down");
    EXPECT_EQ(adminState(json::parse(R"({"@name":"ethernet1/1"})")), "up");
}

TEST(Ike, GatewayCarriesItsInterfaceAndClassifiedPeer)
{
    const json doc = json::parse(R"({"result":{"entry":[
      {"@name":"SC-sherpain","local-address":{"ip":"1.214.192.59/28","interface":"ethernet1/2"},
       "peer-address":{"fqdn":"sc-sherpain.south-korea.sc.gyoyy52oy.gpcloudservice.com"},
       "protocol":{"version":"ikev2"}},
      {"@name":"arbor","local-address":{"ip":"1.214.192.59/28","interface":"ethernet1/2"},
       "peer-address":{"ip":"13.124.66.36"}}
    ]}})");

    const auto gws = readIkeGateways(doc);
    ASSERT_EQ(gws.size(), 2u);

    EXPECT_EQ(gws[0].interfaceName, "ethernet1/2");
    EXPECT_EQ(gws[0].localIp, "1.214.192.59/28");
    EXPECT_EQ(gws[0].peer.kind, PeerKind::ServiceConnection);
    EXPECT_EQ(gws[0].peer.tenant, "gyoyy52oy");
    EXPECT_EQ(gws[0].version, "ikev2");

    EXPECT_EQ(gws[1].peer.kind, PeerKind::External);
    EXPECT_EQ(gws[1].peer.addr, "13.124.66.36");
}

TEST(GlobalProtect, PortalReportsItsInterfaceAndTheUnionOfAdvertisedGateways)
{
    // Two client configurations naming an overlapping gateway set — the portal advertises the
    // union, and listing a gateway twice because two configs mention it would be noise.
    const json doc = json::parse(R"({"result":{"entry":[{
      "@name":"Sherpain-PO",
      "portal-config":{"local-address":{"ip":{"ipv4":"192.168.1.1/23"},"interface":"ethernet1/1"}},
      "client-config":{"configs":{"entry":[
        {"@name":"a","gateways":{"external":{"list":{"entry":[
           {"@name":"ext","ip":{"ipv4":"1.214.192.59:12929"}}]}}}},
        {"@name":"b","gateways":{"external":{"list":{"entry":[
           {"@name":"ext","ip":{"ipv4":"1.214.192.59:12929"}},
           {"@name":"jp-gw","ip":{"ipv4":"18.176.31.154"}}]}}}}
      ]}}
    }]}})");

    const auto portals = readGpPortals(doc);
    ASSERT_EQ(portals.size(), 1u);
    EXPECT_EQ(portals[0].interfaceName, "ethernet1/1");
    EXPECT_EQ(portals[0].localIp, "192.168.1.1/23");
    ASSERT_EQ(portals[0].gateways.size(), 2u);
    EXPECT_EQ(portals[0].gateways[0], "ext @ 1.214.192.59:12929");
    EXPECT_EQ(portals[0].gateways[1], "jp-gw @ 18.176.31.154");
}

TEST(GlobalProtect, GatewayCollectsEveryClientPoolAcrossItsConfigs)
{
    const json doc = json::parse(R"({"result":{"entry":[{
      "@name":"Sherpain_GW","tunnel-mode":"yes",
      "remote-user-tunnel-configs":{"entry":[
        {"@name":"yhlee","ip-pool":{"member":["172.16.110.0/29"]}},
        {"@name":"pre","ip-pool":{"member":["172.16.26.0/29","172.16.28.0/25"]}}
      ]}
    }]}})");

    const auto gws = readGpGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    EXPECT_TRUE(gws[0].tunnelMode);
    ASSERT_EQ(gws[0].pools.size(), 3u);
    EXPECT_EQ(gws[0].pools[0], "172.16.110.0/29");
    EXPECT_EQ(gws[0].pools[2], "172.16.28.0/25");
}

TEST(GlobalProtect, GatewayWithoutALocalAddressLeavesItBlankRatherThanInventingOne)
{
    // Real case: a gateway whose local-address is absent entirely. It still exists and belongs in
    // the picture — it just cannot be pinned to an interface.
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"Sherpain_GW","tunnel-mode":"yes"}]}})");
    const auto gws = readGpGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    EXPECT_EQ(gws[0].interfaceName, "");
    EXPECT_EQ(gws[0].localIp, "");
}

// The role names are a contract with the page's CSS (.topo-port.role-<name>), so they are pinned
// here rather than left to be discovered by a class that silently fails to match.
TEST(InterfaceRole, NamesAreStableAndDistinct)
{
    EXPECT_STREQ(ifRoleName(IfRole::Wan), "wan");
    EXPECT_STREQ(ifRoleName(IfRole::Edge), "edge");
    EXPECT_STREQ(ifRoleName(IfRole::Lan), "lan");
    EXPECT_STREQ(ifRoleName(IfRole::Loopback), "loopback");
    EXPECT_STREQ(ifRoleName(IfRole::Tunnel), "tunnel");
    EXPECT_STREQ(ifRoleName(IfRole::Unknown), "unknown");
}

// The peer-kind names are the other half of that contract — they go into the topology document as
// `kind`, which the page branches on to decide which lane an endpoint is drawn in.
TEST(PeerKindNames, AreStableAndDistinct)
{
    EXPECT_STREQ(peerKindName(PeerKind::ServiceConnection), "service_connection");
    EXPECT_STREQ(peerKindName(PeerKind::RemoteNetwork), "remote_network");
    EXPECT_STREQ(peerKindName(PeerKind::External), "external");
    EXPECT_STREQ(peerKindName(PeerKind::Unknown), "unknown");

    // An enumerator added without a name here would fall through to "unknown" and quietly land
    // every such peer in the wrong lane rather than failing.
    EXPECT_STRNE(peerKindName(PeerKind::ServiceConnection), peerKindName(PeerKind::RemoteNetwork));
}

// ── str(): one field, whatever type the vendor used ───────────────────────────────────────────

TEST(Str, ReturnsAStringFieldVerbatim)
{
    EXPECT_EQ(str(json::parse(R"({"@name":"ethernet1/1"})"), "@name"), "ethernet1/1");
    EXPECT_EQ(str(json::parse(R"({"@name":""})"), "@name"), "");
}

TEST(Str, RendersANonStringFieldRatherThanDroppingIt)
{
    // PAN-OS is inconsistent about quoting: the same field comes back as a string on one release
    // and a number or bool on another. Returning "" for those would silently empty the model.
    EXPECT_EQ(str(json::parse(R"({"mtu":1500})"), "mtu"), "1500");
    EXPECT_EQ(str(json::parse(R"({"enabled":true})"), "enabled"), "true");
    EXPECT_EQ(str(json::parse(R"({"member":["a","b"]})"), "member"), R"(["a","b"])");
}

TEST(Str, AbsentNullOrNonObjectYieldsEmpty)
{
    EXPECT_EQ(str(json::parse(R"({"a":1})"), "b"), "");
    EXPECT_EQ(str(json::parse(R"({"a":null})"), "a"), "");
    EXPECT_EQ(str(json::array({1, 2}), "a"), "");
    EXPECT_EQ(str(json("a string"), "a"), "");
    EXPECT_EQ(str(json(nullptr), "a"), "");
}

// ── entriesOf(): the PAN-OS list-or-object shape ──────────────────────────────────────────────

TEST(Entries, AListComesBackUnchanged)
{
    const auto e = entriesOf(json::parse(R"({"result":{"entry":[{"@name":"a"},{"@name":"b"}]}})"));
    ASSERT_EQ(e.size(), 2u);
    EXPECT_EQ(str(e[0], "@name"), "a");
    EXPECT_EQ(str(e[1], "@name"), "b");
}

TEST(Entries, WorksWithOrWithoutTheResultWrapper)
{
    // Some collected documents are already unwrapped by the caller; both shapes have to read the
    // same or half the readers would see nothing.
    const auto wrapped = entriesOf(json::parse(R"({"result":{"entry":[{"@name":"a"}]}})"));
    const auto bare = entriesOf(json::parse(R"({"entry":[{"@name":"a"}]})"));
    ASSERT_EQ(wrapped.size(), 1u);
    ASSERT_EQ(bare.size(), 1u);
    EXPECT_EQ(str(wrapped[0], "@name"), str(bare[0], "@name"));
}

TEST(Entries, AnythingThatIsNotAListOrObjectYieldsNoEntries)
{
    EXPECT_TRUE(entriesOf(json::parse(R"({"result":{"entry":"a string"}})")).empty());
    EXPECT_TRUE(entriesOf(json::parse(R"({"result":{"entry":42}})")).empty());
    EXPECT_TRUE(entriesOf(json::parse(R"({"result":{}})")).empty());
    EXPECT_TRUE(entriesOf(json::array({1, 2})).empty());
    EXPECT_TRUE(entriesOf(json("a string")).empty());
    EXPECT_TRUE(entriesOf(json(nullptr)).empty());
}

TEST(Entries, ANonObjectResultIsNotTreatedAsTheWrapper)
{
    // {"result":"..."} with a sibling entry: the wrapper is only honoured when it is an object, so
    // the top-level entry is what should be read.
    const auto e = entriesOf(json::parse(R"({"result":"none","entry":[{"@name":"a"}]})"));
    ASSERT_EQ(e.size(), 1u);
    EXPECT_EQ(str(e[0], "@name"), "a");
}

// ── interfaceMode() / interfaceAddresses() ────────────────────────────────────────────────────

TEST(Interfaces, ModeIsWhicheverNodeThePanOsDocumentCarries)
{
    for (const char* mode : {"layer3", "layer2", "tap", "ha", "virtual-wire", "aggregate-group"})
    {
        json e = json::object();
        e[mode] = json::object();
        EXPECT_EQ(interfaceMode(e), mode) << mode;
    }

    EXPECT_EQ(interfaceMode(json::parse(R"({"@name":"ethernet1/1"})")), "") << "no mode node at all";
}

TEST(Interfaces, ModeIsReportedInAFixedOrderWhenSeveralNodesArePresent)
{
    // Malformed but seen: a document carrying two mode nodes. The answer has to be deterministic,
    // or the same firewall renders differently between polls.
    const json e = json::parse(R"({"layer2":{},"layer3":{}})");
    EXPECT_EQ(interfaceMode(e), "layer3");
}

TEST(Interfaces, AggregateGroupIsAModeButCarriesNoAddressesOfItsOwn)
{
    // A deliberate asymmetry: aggregate-group names the bundle an interface belongs to, and the
    // addresses live on the bundle, not here. Reporting the mode while returning no address is
    // the honest answer.
    const json e = json::parse(R"({"@name":"ethernet1/3","aggregate-group":"ae1"})");
    EXPECT_EQ(interfaceMode(e), "aggregate-group");
    EXPECT_TRUE(interfaceAddresses(e).empty());
}

TEST(Interfaces, AddressesAreReadFromTheFirstModeThatHasAny)
{
    const json e = json::parse(R"({"@name":"ethernet1/4",
      "layer3":{"ip":{"entry":[{"@name":"10.1.1.1/24"}]}}})");

    const auto addrs = interfaceAddresses(e);
    ASSERT_EQ(addrs.size(), 1u);
    EXPECT_EQ(addrs[0], "10.1.1.1/24");
}

TEST(Interfaces, ASingleAddressObjectIsNormalisedLikeAList)
{
    // PAN-OS collapses a one-element list to an object here as everywhere else.
    const json e = json::parse(R"({"layer3":{"ip":{"entry":{"@name":"10.2.2.2/30"}}}})");
    const auto addrs = interfaceAddresses(e);
    ASSERT_EQ(addrs.size(), 1u);
    EXPECT_EQ(addrs[0], "10.2.2.2/30");
}

TEST(Interfaces, AnEntryWithoutANameContributesNoAddress)
{
    // The address IS the entry name. An entry without one carries no address to report, and must
    // not push an empty string that the page would render as a blank chip.
    const json e = json::parse(R"({"layer3":{"ip":{"entry":[{"@name":""},{"comment":"x"}]}}})");
    EXPECT_TRUE(interfaceAddresses(e).empty());
}

TEST(Interfaces, AModeWithNoIpNodeYieldsNoAddresses)
{
    EXPECT_TRUE(interfaceAddresses(json::parse(R"({"layer3":{}})")).empty());
    EXPECT_TRUE(interfaceAddresses(json::parse(R"({"layer3":"enabled"})")).empty()) << "mode is not an object";
    EXPECT_TRUE(interfaceAddresses(json::parse(R"({"@name":"loopback.1"})")).empty()) << "no mode node";
}

TEST(Interfaces, AdminStateReadsLinkStateCaseInsensitivelyAndAheadOfDisabled)
{
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"DOWN"})")), "down");
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"Up"})")), "up");

    // link-state is the more specific statement, so it wins over the generic disabled flag.
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"up","disabled":"yes"})")), "up");
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"down","disabled":"no"})")), "down");
}

TEST(Interfaces, AdminStateTreatsOnlyAnExplicitDisabledFlagAsDown)
{
    EXPECT_EQ(adminState(json::parse(R"({"disabled":"yes"})")), "down");
    EXPECT_EQ(adminState(json::parse(R"({"disabled":"true"})")), "down");
    EXPECT_EQ(adminState(json::parse(R"({"disabled":"no"})")), "up");
    EXPECT_EQ(adminState(json::parse(R"({"disabled":""})")), "up");

    // An unrecognised link-state says nothing either way, so the default stands.
    EXPECT_EQ(adminState(json::parse(R"({"link-state":"auto"})")), "up");
    EXPECT_EQ(adminState(json::object()), "up");
}

// ── readIkeGateways() ─────────────────────────────────────────────────────────────────────────

TEST(Ike, PeerAddressPrefersTheIpFieldOverTheFqdn)
{
    // Both present is unusual but legal. The IP is the one the tunnel actually uses.
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"g",
      "peer-address":{"ip":"203.0.113.7","fqdn":"hq.south-korea.sc.abc123.gpcloudservice.com"}}]}})");

    const auto gws = readIkeGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    EXPECT_EQ(gws[0].peer.addr, "203.0.113.7");
    EXPECT_EQ(gws[0].peer.kind, PeerKind::External);
}

TEST(Ike, AGatewayWithNoPeerAddressIsUnknownRatherThanDropped)
{
    // It is still a configured gateway; it just has nothing on the far end we can name.
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"half-built",
      "local-address":{"interface":"ethernet1/2"}}]}})");

    const auto gws = readIkeGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    EXPECT_EQ(gws[0].name, "half-built");
    EXPECT_EQ(gws[0].peer.kind, PeerKind::Unknown);
    EXPECT_EQ(gws[0].interfaceName, "ethernet1/2");
    EXPECT_EQ(gws[0].localIp, "") << "no ip node means no address, not a guessed one";
}

TEST(Ike, NonObjectEntriesAreSkippedWithoutLosingTheRest)
{
    const json doc = json::parse(R"({"result":{"entry":[
      "garbage", {"@name":"real","peer-address":{"ip":"198.51.100.1"}}, 42]}})");

    const auto gws = readIkeGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    EXPECT_EQ(gws[0].name, "real");
}

TEST(Ike, VersionIsBlankWhenTheProtocolNodeIsAbsent)
{
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"g","peer-address":{"ip":"198.51.100.1"}}]}})");
    EXPECT_EQ(readIkeGateways(doc)[0].version, "");
}

// ── readGpPortals() / readGpGateways() ────────────────────────────────────────────────────────

TEST(GlobalProtect, PortalUnionCoversInternalGatewaysAsWellAsExternal)
{
    const json doc = json::parse(R"({"result":{"entry":[{
      "@name":"P",
      "client-config":{"configs":{"entry":[
        {"@name":"a","gateways":{
           "external":{"list":{"entry":[{"@name":"ext","ip":{"ipv4":"203.0.113.1"}}]}},
           "internal":{"list":{"entry":[{"@name":"int","ip":{"ipv4":"10.0.0.1"}}]}}}}
      ]}}
    }]}})");

    const auto portals = readGpPortals(doc);
    ASSERT_EQ(portals.size(), 1u);
    ASSERT_EQ(portals[0].gateways.size(), 2u);
    EXPECT_EQ(portals[0].gateways[0], "ext @ 203.0.113.1");
    EXPECT_EQ(portals[0].gateways[1], "int @ 10.0.0.1");
}

TEST(GlobalProtect, PortalGatewayFallsBackToItsFqdnAndThenToItsNameAlone)
{
    const json doc = json::parse(R"({"result":{"entry":[{
      "@name":"P",
      "client-config":{"configs":{"entry":[
        {"@name":"a","gateways":{"external":{"list":{"entry":[
           {"@name":"by-fqdn","fqdn":"gw.example.com"},
           {"@name":"nameless-address-less"}]}}}}
      ]}}
    }]}})");

    const auto portals = readGpPortals(doc);
    ASSERT_EQ(portals.size(), 1u);
    ASSERT_EQ(portals[0].gateways.size(), 2u);
    EXPECT_EQ(portals[0].gateways[0], "by-fqdn @ gw.example.com");
    EXPECT_EQ(portals[0].gateways[1], "nameless-address-less") << "a named gateway with no address still counts";
}

TEST(GlobalProtect, PortalWithNoClientConfigStillAppearsWithAnEmptyGatewayList)
{
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"P",
      "portal-config":{"local-address":{"ip":{"ipv4":"192.168.1.1/23"},"interface":"ethernet1/1"}}}]}})");

    const auto portals = readGpPortals(doc);
    ASSERT_EQ(portals.size(), 1u);
    EXPECT_EQ(portals[0].name, "P");
    EXPECT_EQ(portals[0].interfaceName, "ethernet1/1");
    EXPECT_TRUE(portals[0].gateways.empty());
}

TEST(GlobalProtect, GatewayTunnelModeIsOnlyTrueForAnAffirmativeValue)
{
    auto tunnelMode = [](const char* value)
    {
        const json doc = json::parse(std::string(R"({"result":{"entry":[{"@name":"g","tunnel-mode":")") + value +
                                     R"("}]}})");
        return readGpGateways(doc)[0].tunnelMode;
    };

    EXPECT_TRUE(tunnelMode("yes"));
    EXPECT_TRUE(tunnelMode("true"));
    EXPECT_FALSE(tunnelMode("no"));
    EXPECT_FALSE(tunnelMode(""));
    EXPECT_FALSE(tunnelMode("Yes")) << "PAN-OS writes these lower-case; anything else is not an affirmation";

    const json doc = json::parse(R"({"result":{"entry":[{"@name":"g"}]}})");
    EXPECT_FALSE(readGpGateways(doc)[0].tunnelMode) << "absent means not tunnelled";
}

TEST(GlobalProtect, GatewayAcceptsASinglePoolWrittenAsAStringRatherThanAList)
{
    // The same list-collapsing PAN-OS does everywhere, one level down in the pool member.
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"g",
      "remote-user-tunnel-configs":{"entry":[{"@name":"c","ip-pool":{"member":"172.16.1.0/24"}}]}}]}})");

    const auto gws = readGpGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    ASSERT_EQ(gws[0].pools.size(), 1u);
    EXPECT_EQ(gws[0].pools[0], "172.16.1.0/24");
}

TEST(GlobalProtect, GatewayConfigWithoutAPoolContributesNothingAndBreaksNothing)
{
    const json doc = json::parse(R"({"result":{"entry":[{"@name":"g",
      "remote-user-tunnel-configs":{"entry":[
        {"@name":"no-pool"},
        {"@name":"empty-pool","ip-pool":{}},
        {"@name":"real","ip-pool":{"member":["172.16.2.0/24"]}}]}}]}})");

    const auto gws = readGpGateways(doc);
    ASSERT_EQ(gws.size(), 1u);
    ASSERT_EQ(gws[0].pools.size(), 1u);
    EXPECT_EQ(gws[0].pools[0], "172.16.2.0/24");
}

TEST(GlobalProtect, ReadersReturnNothingForAnEmptyDocumentRatherThanThrowing)
{
    // A firewall with no GlobalProtect configured answers with an empty result, and that is a
    // normal answer — most of the estate is not running GlobalProtect at all.
    for (const char* body : {R"({"result":{}})", R"({"result":{"entry":[]}})", R"({})", "null"})
    {
        const json doc = json::parse(body);
        EXPECT_TRUE(readGpPortals(doc).empty()) << body;
        EXPECT_TRUE(readGpGateways(doc).empty()) << body;
        EXPECT_TRUE(readIkeGateways(doc).empty()) << body;
    }
}
