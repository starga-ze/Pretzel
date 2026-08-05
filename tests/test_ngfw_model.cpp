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
