// Covers pz::ipc::IpcProtocol — the wire header's flag helpers, byte-order conversion and
// enum/string mappings.
//
// This is a hand-rolled protocol shared by eight daemons, so its rules live nowhere but in this
// code. Two of them are worth pinning hard: the header is exactly 16 bytes (any drift silently
// desynchronises every peer), and hostToNet/netToHost must round-trip (a mistake here turns
// into "works on this machine" and nothing else).

#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace pz::ipc;

// ── Wire header layout ──────────────────────────────────────────────────────────────────

TEST(IpcWireHeaderLayout, IsSixteenBytes)
{
    // Also asserted statically in the header; repeated here so a layout change fails a test
    // with a readable name rather than only a compile error deep in an include chain.
    EXPECT_EQ(16u, sizeof(IpcWireHeader));
}

TEST(IpcWireHeaderLayout, ProtocolVersionIsTheExpectedOne)
{
    // A bump is intentional and must be deliberate: every daemon has to ship together, since
    // decode() rejects a frame whose version differs.
    EXPECT_EQ(2, IPC_PROTOCOL_VERSION);
}

// ── Flags ───────────────────────────────────────────────────────────────────────────────

TEST(IpcProtocolFlags, ToFlagProducesTheDocumentedBits)
{
    EXPECT_EQ(0x00, IpcProtocol::toFlag(IpcFlag::None));
    EXPECT_EQ(0x01, IpcProtocol::toFlag(IpcFlag::Request));
    EXPECT_EQ(0x02, IpcProtocol::toFlag(IpcFlag::Response));
    EXPECT_EQ(0x04, IpcProtocol::toFlag(IpcFlag::Error));
    EXPECT_EQ(0x08, IpcProtocol::toFlag(IpcFlag::Broadcast));
}

TEST(IpcProtocolFlags, OrFlagCombinesBoth)
{
    const auto combined = IpcProtocol::orFlag(IpcFlag::Response, IpcFlag::Error);

    EXPECT_TRUE(IpcProtocol::hasFlag(combined, IpcFlag::Response));
    EXPECT_TRUE(IpcProtocol::hasFlag(combined, IpcFlag::Error));
}

TEST(IpcProtocolFlags, OrFlagDoesNotSetUnrelatedBits)
{
    // engined sends Response|Error for a failed request; it must not read as a Request.
    const auto combined = IpcProtocol::orFlag(IpcFlag::Response, IpcFlag::Error);

    EXPECT_FALSE(IpcProtocol::hasFlag(combined, IpcFlag::Request));
    EXPECT_FALSE(IpcProtocol::hasFlag(combined, IpcFlag::Broadcast));
}

TEST(IpcProtocolFlags, HasFlagIsFalseOnAnEmptyFlagSet)
{
    EXPECT_FALSE(IpcProtocol::hasFlag(0, IpcFlag::Request));
    EXPECT_FALSE(IpcProtocol::hasFlag(0, IpcFlag::Response));
}

TEST(IpcProtocolFlags, HasFlagIgnoresOtherBitsBeingSet)
{
    const std::uint8_t all = 0xFF;

    EXPECT_TRUE(IpcProtocol::hasFlag(all, IpcFlag::Request));
    EXPECT_TRUE(IpcProtocol::hasFlag(all, IpcFlag::Broadcast));
}

TEST(IpcProtocolFlags, FlagsToStrNamesEverySetBit)
{
    const std::string s = IpcProtocol::flagsToStr(IpcProtocol::orFlag(IpcFlag::Response, IpcFlag::Error));

    EXPECT_NE(std::string::npos, s.find("Response")) << "got: " << s;
    EXPECT_NE(std::string::npos, s.find("Error")) << "got: " << s;
}

// ── Byte order ──────────────────────────────────────────────────────────────────────────

namespace
{

IpcWireHeader sampleHeader()
{
    IpcWireHeader h{};
    h.version = IPC_PROTOCOL_VERSION;
    h.src = static_cast<std::uint8_t>(IpcDaemon::Mgmtd);
    h.dst = static_cast<std::uint8_t>(IpcDaemon::Collectord);
    h.flags = IpcProtocol::toFlag(IpcFlag::Request);
    h.cmd = static_cast<std::uint16_t>(IpcCmd::ApiKeygenRequest);
    h.reserved = 0;
    h.seqNo = 0x12345678u;
    h.payloadLen = 0x00ABCDEFu;
    return h;
}

}

TEST(IpcProtocolByteOrder, HostToNetThenNetToHostRoundTrips)
{
    const IpcWireHeader original = sampleHeader();
    const IpcWireHeader restored = IpcProtocol::netToHost(IpcProtocol::hostToNet(original));

    EXPECT_EQ(original.version, restored.version);
    EXPECT_EQ(original.src, restored.src);
    EXPECT_EQ(original.dst, restored.dst);
    EXPECT_EQ(original.flags, restored.flags);
    EXPECT_EQ(original.cmd, restored.cmd);
    EXPECT_EQ(original.reserved, restored.reserved);
    EXPECT_EQ(original.seqNo, restored.seqNo);
    EXPECT_EQ(original.payloadLen, restored.payloadLen);
}

TEST(IpcProtocolByteOrder, SingleByteFieldsAreUntouched)
{
    // Byte-order conversion applies to the multi-byte fields only; converting a uint8 would be
    // a no-op that still signals confusion about the layout.
    const IpcWireHeader net = IpcProtocol::hostToNet(sampleHeader());

    EXPECT_EQ(IPC_PROTOCOL_VERSION, net.version);
    EXPECT_EQ(static_cast<std::uint8_t>(IpcDaemon::Mgmtd), net.src);
    EXPECT_EQ(static_cast<std::uint8_t>(IpcDaemon::Collectord), net.dst);
}

TEST(IpcProtocolByteOrder, RoundTripSurvivesExtremeValues)
{
    IpcWireHeader h{};
    h.version = IPC_PROTOCOL_VERSION;
    h.cmd = 0xFFFF;
    h.seqNo = 0xFFFFFFFFu;
    h.payloadLen = 0xFFFFFFFFu;

    const IpcWireHeader restored = IpcProtocol::netToHost(IpcProtocol::hostToNet(h));

    EXPECT_EQ(0xFFFF, restored.cmd);
    EXPECT_EQ(0xFFFFFFFFu, restored.seqNo);
    EXPECT_EQ(0xFFFFFFFFu, restored.payloadLen);
}

// ── Enum ↔ string ───────────────────────────────────────────────────────────────────────

TEST(IpcProtocolNaming, DaemonRoundTripsThroughItsName)
{
    const IpcDaemon daemons[] = {IpcDaemon::Ipcd,  IpcDaemon::Engined, IpcDaemon::Mgmtd,
                                 IpcDaemon::Authd, IpcDaemon::Probed,   IpcDaemon::Collectord,
                                 IpcDaemon::Apid};

    for (IpcDaemon d : daemons)
    {
        const std::string name = IpcProtocol::daemonToStr(d);
        EXPECT_EQ(d, IpcProtocol::strToDaemon(name)) << "round trip failed for " << name;
    }
}

TEST(IpcProtocolNaming, UnknownDaemonNameIsRejectedRatherThanGuessed)
{
    EXPECT_EQ(IpcDaemon::Unknown, IpcProtocol::strToDaemon("nope"));
    EXPECT_EQ(IpcDaemon::Unknown, IpcProtocol::strToDaemon(""));
}

TEST(IpcProtocolNaming, EveryCommandHasAName)
{
    // A command added to the enum but not to cmdToStr shows up as the fallback string, which
    // makes every log line about it useless. These are the ones the API connector path uses —
    // one request command per operation, all answered by the single ApiConnectorTestResponse.
    const IpcCmd cmds[] = {IpcCmd::ClientHello,
                           IpcCmd::ServerHello,
                           IpcCmd::LocalUserUpdate,
                           IpcCmd::ApiCredentialStateUpdate,
                           IpcCmd::ApiKeygenRequest,
                           IpcCmd::ApiEndpointTestRequest,
                           IpcCmd::ApiSaseTestRequest,
                           IpcCmd::ApiTlsProbeRequest,
                           IpcCmd::ApiSaseKeyStoreRequest,
                           IpcCmd::ApiCredentialStoreRequest,
                           IpcCmd::ApiConnectorTestResponse,
                           IpcCmd::AuthSamlAcsRequest,
                           IpcCmd::AuthSamlAcsResponse};

    for (IpcCmd c : cmds)
    {
        const std::string name = IpcProtocol::cmdToStr(c);
        EXPECT_FALSE(name.empty());
        EXPECT_EQ(std::string::npos, name.find("Unknown"))
            << "cmd " << static_cast<int>(c) << " has no name of its own";
    }
}

TEST(IpcProtocolNaming, DistinctCommandsHaveDistinctNames)
{
    EXPECT_STRNE(IpcProtocol::cmdToStr(IpcCmd::ApiKeygenRequest),
                 IpcProtocol::cmdToStr(IpcCmd::ApiConnectorTestResponse));
    EXPECT_STRNE(IpcProtocol::cmdToStr(IpcCmd::ApiCredentialStateUpdate),
                 IpcProtocol::cmdToStr(IpcCmd::LocalUserUpdate));
}

// ── Message flags ─────────────────────────────────────────────────────────────────────────────
//
// The flag byte is how a receiver tells an answer from a question. Every router in the system
// branches on these, and they are a bitfield rather than an enum precisely so an error response
// can be both — so the predicates have to be independent of each other, not a switch in disguise.

TEST(IpcMessageFlags, EachPredicateAnswersForItsOwnFlagOnly)
{
    IpcMessage msg;

    msg.setFlags(IpcProtocol::toFlag(IpcFlag::Request));
    EXPECT_TRUE(msg.isRequest());
    EXPECT_FALSE(msg.isResponse());
    EXPECT_FALSE(msg.isError());
    EXPECT_FALSE(msg.isBroadcast());

    msg.setFlags(IpcProtocol::toFlag(IpcFlag::Response));
    EXPECT_FALSE(msg.isRequest());
    EXPECT_TRUE(msg.isResponse());
    EXPECT_FALSE(msg.isError());
    EXPECT_FALSE(msg.isBroadcast());

    msg.setFlags(IpcProtocol::toFlag(IpcFlag::Error));
    EXPECT_FALSE(msg.isRequest());
    EXPECT_FALSE(msg.isResponse());
    EXPECT_TRUE(msg.isError());
    EXPECT_FALSE(msg.isBroadcast());

    msg.setFlags(IpcProtocol::toFlag(IpcFlag::Broadcast));
    EXPECT_FALSE(msg.isRequest());
    EXPECT_FALSE(msg.isResponse());
    EXPECT_FALSE(msg.isError());
    EXPECT_TRUE(msg.isBroadcast());
}

TEST(IpcMessageFlags, NoFlagsMeansNoneOfThePredicatesHold)
{
    IpcMessage msg;
    msg.setFlags(IpcProtocol::toFlag(IpcFlag::None));

    EXPECT_FALSE(msg.isRequest());
    EXPECT_FALSE(msg.isResponse());
    EXPECT_FALSE(msg.isError());
    EXPECT_FALSE(msg.isBroadcast());
}

TEST(IpcMessageFlags, UnknownBitsDoNotSatisfyAnyPredicate)
{
    // A peer running a newer build may set bits this one does not know. They must be ignored, not
    // read as whichever flag happens to share a byte with them.
    IpcMessage msg;
    msg.setFlags(0xF0);

    EXPECT_FALSE(msg.isRequest());
    EXPECT_FALSE(msg.isResponse());
    EXPECT_FALSE(msg.isError());
    EXPECT_FALSE(msg.isBroadcast());
}

TEST(IpcMessageFlags, EveryFlagOccupiesItsOwnBit)
{
    const IpcFlag all[] = {IpcFlag::Request, IpcFlag::Response, IpcFlag::Error, IpcFlag::Broadcast};

    std::uint8_t seen = 0;
    for (IpcFlag f : all)
    {
        const std::uint8_t bit = IpcProtocol::toFlag(f);
        EXPECT_NE(0u, bit);
        EXPECT_EQ(0u, bit & (bit - 1)) << "flag " << int(bit) << " is not a single bit";
        EXPECT_EQ(0u, seen & bit) << "flag " << int(bit) << " collides with an earlier one";
        seen = static_cast<std::uint8_t>(seen | bit);
    }
}

TEST(IpcMessageFlags, SurviveTheWireHeaderRoundTrip)
{
    IpcMessage msg;
    msg.setSrc(IpcDaemon::Mgmtd);
    msg.setDst(IpcDaemon::Collectord);
    msg.setCmd(IpcCmd::ApiConnectorTestResponse);
    msg.setSeqNo(7);
    msg.setFlags(IpcProtocol::orFlag(IpcFlag::Response, IpcFlag::Error));

    const IpcWireHeader net = IpcProtocol::hostToNet(msg.toWireHeader());
    const IpcWireHeader host = IpcProtocol::netToHost(net);

    EXPECT_TRUE(IpcProtocol::hasFlag(host.flags, IpcFlag::Response));
    EXPECT_TRUE(IpcProtocol::hasFlag(host.flags, IpcFlag::Error));
    EXPECT_FALSE(IpcProtocol::hasFlag(host.flags, IpcFlag::Request));
    EXPECT_EQ(7u, host.seqNo) << "the flag byte must not have disturbed its neighbours";
}
