// probed's ICMP wire codec: IcmpPacket <-> the bytes that go on a raw socket.
//
// Two things make this worth pinning down. The checksum is the one piece of the echo path a
// device silently ignores when it is wrong — a bad checksum does not error, it just never gets
// answered, and every target then reads as "down". And decode() has to serve two framings from
// the same socket: what we send is bare ICMP, but what the kernel hands back on an IPv4 raw
// socket still carries the IP header, so the codec peels a variable-length prefix that is only
// ever identified by the first nibble of the frame.

#include "icmp/IcmpCodec.h"
#include "icmp/IcmpPacket.h"
#include "icmp/IcmpProtocol.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace pz::probed;

namespace
{

constexpr std::size_t kHeaderLen = IcmpCodec::ICMP_ECHO_HEADER_LEN;

std::vector<std::uint8_t> bytesOf(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

IcmpPacket echoRequest(std::uint16_t id, std::uint16_t seq, const std::vector<std::uint8_t>& payload = {})
{
    return IcmpPacket(IcmpHeader::buildEchoRequest(id, seq), payload);
}

IcmpFrameView view(const std::vector<std::uint8_t>& v)
{
    return IcmpFrameView{v.data(), v.size()};
}

std::uint16_t u16At(const std::vector<std::uint8_t>& f, std::size_t off)
{
    return static_cast<std::uint16_t>((f[off] << 8) | f[off + 1]);
}

// RFC 1071 one's-complement sum, written out independently of the codec's own implementation so
// a bug in that one cannot make these assertions agree with it. Over a frame whose checksum field
// is already filled in, the result is 0xFFFF — that is what "the checksum is right" means.
std::uint16_t onesComplementSum(const std::vector<std::uint8_t>& f)
{
    std::uint32_t sum = 0;
    std::size_t i = 0;
    for (; i + 1 < f.size(); i += 2)
        sum += static_cast<std::uint32_t>((f[i] << 8) | f[i + 1]);
    if (i < f.size())
        sum += static_cast<std::uint32_t>(f[i]) << 8;

    while ((sum >> 16) != 0)
        sum = (sum & 0xFFFFU) + (sum >> 16);

    return static_cast<std::uint16_t>(sum);
}

// A 20-byte IPv4 header with the given IHL (in 32-bit words), as the kernel prepends on an IPv4
// raw socket. Only the first byte matters to the codec; the rest is filler so the length is real.
std::vector<std::uint8_t> ipv4Header(std::uint8_t ihlWords = 5)
{
    std::vector<std::uint8_t> h(static_cast<std::size_t>(ihlWords) * 4U, 0x00);
    h[0] = static_cast<std::uint8_t>(0x40U | (ihlWords & 0x0FU));   // version 4, IHL
    return h;
}

std::vector<std::uint8_t> concat(std::vector<std::uint8_t> a, const std::vector<std::uint8_t>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}

// ── encode: field placement ─────────────────────────────────────────────────────────────────────

TEST(IcmpEncode, EchoRequestWithoutPayloadIsExactlyTheEightByteHeader)
{
    const IcmpCodec codec;
    const auto frame = codec.encode(echoRequest(0xABCD, 7));

    ASSERT_EQ(kHeaderLen, frame.size());
    EXPECT_EQ(8, frame[0]) << "type";
    EXPECT_EQ(0, frame[1]) << "code";
}

TEST(IcmpEncode, IdentifierAndSequenceGoOnTheWireBigEndian)
{
    const IcmpCodec codec;
    const auto frame = codec.encode(echoRequest(0x1234, 0x00FF));

    // Spelled out per byte rather than through a helper: the whole point is that the high byte
    // comes first, so reading it back with the same byte order would prove nothing.
    EXPECT_EQ(0x12, frame[4]);
    EXPECT_EQ(0x34, frame[5]);
    EXPECT_EQ(0x00, frame[6]);
    EXPECT_EQ(0xFF, frame[7]);
}

TEST(IcmpEncode, PayloadFollowsTheHeaderUntouched)
{
    const IcmpCodec codec;
    const auto payload = bytesOf("abcdefgh");
    const auto frame = codec.encode(echoRequest(1, 1, payload));

    ASSERT_EQ(kHeaderLen + payload.size(), frame.size());
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), frame.begin() + kHeaderLen));
}

TEST(IcmpEncode, FrameLengthIsHeaderPlusPayloadForEverySize)
{
    const IcmpCodec codec;
    for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{56}, std::size_t{1472}})
    {
        const std::vector<std::uint8_t> payload(n, 0x5A);
        EXPECT_EQ(kHeaderLen + n, codec.encode(echoRequest(1, 1, payload)).size()) << "payload size " << n;
    }
}

TEST(IcmpEncode, SamePacketAlwaysProducesTheSameBytes)
{
    const IcmpCodec codec;
    const auto payload = bytesOf("deterministic");
    EXPECT_EQ(codec.encode(echoRequest(9, 9, payload)), codec.encode(echoRequest(9, 9, payload)));
}

TEST(IcmpEncode, NullPacketPointerYieldsAnEmptyFrameRatherThanDereferencing)
{
    const IcmpCodec codec;
    const std::unique_ptr<IcmpPacket> none;
    EXPECT_TRUE(codec.encode(none).empty());
}

TEST(IcmpEncode, PointerAndReferenceOverloadsAgree)
{
    const IcmpCodec codec;
    const auto packet = echoRequest(0x4242, 3, bytesOf("same"));
    auto owned = std::make_unique<IcmpPacket>(packet);

    EXPECT_EQ(codec.encode(packet), codec.encode(owned));
}

// ── encode: checksum ────────────────────────────────────────────────────────────────────────────

TEST(IcmpChecksum, EmittedFrameVerifiesToAllOnes)
{
    const IcmpCodec codec;
    for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{8}, std::size_t{55}, std::size_t{56}})
    {
        std::vector<std::uint8_t> payload(n);
        std::iota(payload.begin(), payload.end(), static_cast<std::uint8_t>(1));

        const auto frame = codec.encode(echoRequest(0x1234, 1, payload));
        EXPECT_EQ(0xFFFF, onesComplementSum(frame)) << "payload size " << n;
    }
}

TEST(IcmpChecksum, OddLengthPayloadPadsTheFinalByteHighRatherThanDroppingIt)
{
    const IcmpCodec codec;

    // 0xFF as a lone trailing byte is the case that separates "pad high" from "pad low": read the
    // wrong way it contributes 0x00FF instead of 0xFF00 and the checksum silently differs.
    const auto frame = codec.encode(echoRequest(1, 1, {0xFF}));

    ASSERT_EQ(kHeaderLen + 1, frame.size());
    EXPECT_EQ(0xFFFF, onesComplementSum(frame));
    EXPECT_EQ(0xF8FC, u16At(frame, 2));
}

TEST(IcmpChecksum, KnownFrameMatchesItsHandComputedValue)
{
    const IcmpCodec codec;

    // type 8, code 0, id 0x1234, seq 0x0001, payload "abcdefgh" — computed outside this codebase.
    EXPECT_EQ(0x5435, u16At(codec.encode(echoRequest(0x1234, 1, bytesOf("abcdefgh"))), 2));
    EXPECT_EQ(0x4C2B, u16At(codec.encode(echoRequest(0xABCD, 7)), 2));
}

TEST(IcmpChecksum, ChangingOnePayloadByteChangesTheChecksum)
{
    const IcmpCodec codec;
    auto payload = bytesOf("abcdefgh");
    const auto before = u16At(codec.encode(echoRequest(1, 1, payload)), 2);

    payload[3] ^= 0x01;
    EXPECT_NE(before, u16At(codec.encode(echoRequest(1, 1, payload)), 2));
}

TEST(IcmpChecksum, IsComputedOverTheFrameWithTheFieldZeroedNotWithStaleBytes)
{
    const IcmpCodec codec;

    // A packet carrying a bogus checksum in its header must encode to the same bytes as one with
    // the field left at zero — otherwise re-encoding a decoded packet would fold the old value in.
    auto stale = echoRequest(0x1234, 1, bytesOf("abcdefgh"));
    stale.header().setChecksum(0xDEAD);

    EXPECT_EQ(codec.encode(echoRequest(0x1234, 1, bytesOf("abcdefgh"))), codec.encode(stale));
}

// ── decode: bare ICMP, as we send it ────────────────────────────────────────────────────────────

TEST(IcmpDecode, RoundTripsEveryHeaderFieldAndThePayload)
{
    const IcmpCodec codec;
    const auto payload = bytesOf("round trip");
    const auto frame = codec.encode(echoRequest(0x7F1E, 0x0203, payload));

    std::unique_ptr<IcmpPacket> out;
    ASSERT_EQ(IcmpDecodeResult::Ok, codec.decode(view(frame), out));
    ASSERT_NE(nullptr, out);

    EXPECT_EQ(IcmpType::EchoRequest, out->type());
    EXPECT_EQ(IcmpCode::Echo, out->code());
    EXPECT_EQ(0x7F1E, out->identifier());
    EXPECT_EQ(0x0203, out->sequence());
    EXPECT_EQ(payload, out->payload());
    EXPECT_EQ(u16At(frame, 2), out->checksum()) << "the received checksum is reported, not recomputed";
}

TEST(IcmpDecode, EchoReplyTypeZeroIsNotMistakenForAnIpHeader)
{
    const IcmpCodec codec;

    // The IP-vs-bare decision reads the first nibble. An echo reply starts 0x00, an echo request
    // 0x08 — neither is 4, which is exactly why the heuristic works for the traffic we handle.
    auto reply = echoRequest(5, 5, bytesOf("pong"));
    reply.header().setType(IcmpType::EchoReply);
    const auto frame = codec.encode(reply);

    std::unique_ptr<IcmpPacket> out;
    ASSERT_EQ(IcmpDecodeResult::Ok, codec.decode(view(frame), out));
    ASSERT_NE(nullptr, out);
    EXPECT_EQ(IcmpType::EchoReply, out->type());
    EXPECT_EQ(bytesOf("pong"), out->payload());
}

TEST(IcmpDecode, ExactlyEightBytesIsAValidHeaderWithNoPayload)
{
    const IcmpCodec codec;
    const auto frame = codec.encode(echoRequest(1, 1));
    ASSERT_EQ(kHeaderLen, frame.size());

    std::unique_ptr<IcmpPacket> out;
    ASSERT_EQ(IcmpDecodeResult::Ok, codec.decode(view(frame), out));
    ASSERT_NE(nullptr, out);
    EXPECT_TRUE(out->empty());
}

TEST(IcmpDecode, AnEmptyFrameIsReportedSeparatelyFromAMalformedOne)
{
    const IcmpCodec codec;
    std::unique_ptr<IcmpPacket> out;

    EXPECT_EQ(IcmpDecodeResult::Empty, codec.decode(IcmpFrameView{}, out));
    EXPECT_EQ(nullptr, out);

    const std::vector<std::uint8_t> bytes{0x08};
    EXPECT_EQ(IcmpDecodeResult::Empty, codec.decode(IcmpFrameView{nullptr, bytes.size()}, out));
    EXPECT_EQ(IcmpDecodeResult::Empty, codec.decode(IcmpFrameView{bytes.data(), 0}, out));
}

TEST(IcmpDecode, ShortFrameIsRejected)
{
    const IcmpCodec codec;
    std::unique_ptr<IcmpPacket> out;

    for (std::size_t n = 1; n < kHeaderLen; ++n)
    {
        const std::vector<std::uint8_t> truncated(n, 0x08);
        EXPECT_EQ(IcmpDecodeResult::InvalidFrame, codec.decode(view(truncated), out)) << "size " << n;
        EXPECT_EQ(nullptr, out) << "size " << n;
    }
}

TEST(IcmpDecode, ClearsAnyPreviousPacketEvenWhenItFails)
{
    const IcmpCodec codec;

    auto out = std::make_unique<IcmpPacket>(echoRequest(1, 1, bytesOf("stale")));
    ASSERT_NE(nullptr, out);

    EXPECT_EQ(IcmpDecodeResult::Empty, codec.decode(IcmpFrameView{}, out));
    EXPECT_EQ(nullptr, out) << "a caller that ignores the result must not see the previous packet";
}

TEST(IcmpDecode, AcceptsAFrameWhoseChecksumIsWrong)
{
    const IcmpCodec codec;
    auto frame = codec.encode(echoRequest(1, 1, bytesOf("corrupt me")));
    frame[2] ^= 0xFF;

    // Pinning current behaviour, not endorsing it: decode reports the checksum it read and leaves
    // the verdict to the caller. If validation ever moves in here, this expectation is the thing
    // that should be changed deliberately rather than discovered.
    std::unique_ptr<IcmpPacket> out;
    ASSERT_EQ(IcmpDecodeResult::Ok, codec.decode(view(frame), out));
    ASSERT_NE(nullptr, out);
    EXPECT_NE(0xFFFF, onesComplementSum(frame));
}

// ── decode: IPv4-wrapped, as the kernel returns it ──────────────────────────────────────────────

TEST(IcmpDecodeIpv4, PeelsTheMinimumTwentyByteHeader)
{
    const IcmpCodec codec;
    const auto payload = bytesOf("via raw socket");
    const auto frame = concat(ipv4Header(5), codec.encode(echoRequest(0x1111, 0x2222, payload)));

    std::unique_ptr<IcmpPacket> out;
    ASSERT_EQ(IcmpDecodeResult::Ok, codec.decode(view(frame), out));
    ASSERT_NE(nullptr, out);

    EXPECT_EQ(0x1111, out->identifier());
    EXPECT_EQ(0x2222, out->sequence());
    EXPECT_EQ(payload, out->payload()) << "the IP header must not leak into the payload";
}

TEST(IcmpDecodeIpv4, HonoursTheIhlFieldWhenOptionsArePresent)
{
    const IcmpCodec codec;
    const auto payload = bytesOf("with options");

    // IHL 6..15 means 24..60 bytes of IP header. Taking a fixed 20 would shift every later field.
    for (std::uint8_t ihl = 6; ihl <= 15; ++ihl)
    {
        const auto frame = concat(ipv4Header(ihl), codec.encode(echoRequest(0x0A0B, ihl, payload)));

        std::unique_ptr<IcmpPacket> out;
        ASSERT_EQ(IcmpDecodeResult::Ok, codec.decode(view(frame), out)) << "ihl " << int(ihl);
        ASSERT_NE(nullptr, out) << "ihl " << int(ihl);
        EXPECT_EQ(0x0A0B, out->identifier()) << "ihl " << int(ihl);
        EXPECT_EQ(ihl, out->sequence()) << "ihl " << int(ihl);
        EXPECT_EQ(payload, out->payload()) << "ihl " << int(ihl);
    }
}

TEST(IcmpDecodeIpv4, RejectsAnIhlBelowTheMinimumHeaderLength)
{
    const IcmpCodec codec;
    std::unique_ptr<IcmpPacket> out;

    for (std::uint8_t ihl = 0; ihl < 5; ++ihl)
    {
        auto frame = concat(ipv4Header(5), codec.encode(echoRequest(1, 1, bytesOf("payload"))));
        frame[0] = static_cast<std::uint8_t>(0x40U | ihl);   // claim a header shorter than 20 bytes

        EXPECT_EQ(IcmpDecodeResult::InvalidFrame, codec.decode(view(frame), out)) << "ihl " << int(ihl);
        EXPECT_EQ(nullptr, out) << "ihl " << int(ihl);
    }
}

TEST(IcmpDecodeIpv4, RejectsAnIhlThatRunsPastTheEndOfTheFrame)
{
    const IcmpCodec codec;

    auto frame = concat(ipv4Header(5), codec.encode(echoRequest(1, 1)));
    frame[0] = 0x4F;   // IHL 15 = 60 bytes, but the frame is only 28

    std::unique_ptr<IcmpPacket> out;
    EXPECT_EQ(IcmpDecodeResult::InvalidFrame, codec.decode(view(frame), out));
    EXPECT_EQ(nullptr, out);
}

TEST(IcmpDecodeIpv4, RejectsAFrameWithNoRoomForIcmpAfterTheIpHeader)
{
    const IcmpCodec codec;
    std::unique_ptr<IcmpPacket> out;

    // 20-byte header plus fewer than 8 bytes: long enough to look like IPv4, too short to carry an
    // echo header. Rejecting here is what stops the decoder reading past the buffer.
    for (std::size_t tail = 0; tail < kHeaderLen; ++tail)
    {
        const auto frame = concat(ipv4Header(5), std::vector<std::uint8_t>(tail, 0x00));
        EXPECT_EQ(IcmpDecodeResult::InvalidFrame, codec.decode(view(frame), out)) << "tail " << tail;
    }
}

TEST(IcmpDecodeIpv4, AFrameShorterThanTwentyBytesIsRejectedRatherThanTreatedAsBareIcmp)
{
    const IcmpCodec codec;

    // First nibble 4 commits the frame to the IPv4 path; there is no falling back to "maybe it was
    // bare ICMP after all", because an ICMP type of 0x4X is not one we ever send or expect.
    std::vector<std::uint8_t> frame(kHeaderLen + 4, 0x00);
    frame[0] = 0x45;

    std::unique_ptr<IcmpPacket> out;
    EXPECT_EQ(IcmpDecodeResult::InvalidFrame, codec.decode(view(frame), out));
}

// ── protocol names ──────────────────────────────────────────────────────────────────────────────

TEST(IcmpProtocolNames, EveryDeclaredTypeHasItsOwnName)
{
    const IcmpType types[] = {IcmpType::EchoReply, IcmpType::DestinationUnreachable, IcmpType::Redirect,
                              IcmpType::EchoRequest, IcmpType::TimeExceeded};

    for (IcmpType t : types)
    {
        const std::string name = IcmpProtocol::typeToStr(t);
        EXPECT_FALSE(name.empty());
        EXPECT_NE("Unknown", name) << "type " << static_cast<int>(t);
    }

    EXPECT_STREQ("Unknown", IcmpProtocol::typeToStr(static_cast<IcmpType>(200)));
}

TEST(IcmpProtocolNames, EchoCodeIsNamedOnlyForTheEchoTypes)
{
    EXPECT_STREQ("Echo", IcmpProtocol::codeToStr(IcmpType::EchoRequest, IcmpCode::Echo));
    EXPECT_STREQ("Echo", IcmpProtocol::codeToStr(IcmpType::EchoReply, IcmpCode::Echo));

    // Code 0 means something else entirely under these types, so it must not read back as "Echo".
    EXPECT_STREQ("Unknown", IcmpProtocol::codeToStr(IcmpType::DestinationUnreachable, IcmpCode::Echo));
    EXPECT_STREQ("Unknown", IcmpProtocol::codeToStr(IcmpType::TimeExceeded, IcmpCode::Echo));
}
