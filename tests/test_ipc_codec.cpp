// Covers pz::ipc::IpcCodec — framing for the hand-rolled IPC protocol.
//
// Everything the daemons say to each other passes through here. The failure modes that matter
// are not "does a good frame decode" but the boundaries: a frame that arrived half-read, a
// length field that lies, a version from an older binary. Getting those wrong desynchronises a
// stream rather than dropping one message, so each is pinned separately.

#include "ipc/IpcCodec.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace pz::ipc;

namespace
{

constexpr std::size_t kHeaderLen = sizeof(IpcWireHeader);

std::unique_ptr<IpcMessage> makeMessage(const std::string& payload,
                                        IpcCmd cmd = IpcCmd::ApiConnectorTestRequest,
                                        std::uint32_t seqNo = 42)
{
    auto msg = std::make_unique<IpcMessage>();
    msg->setSrc(IpcDaemon::Mgmtd);
    msg->setDst(IpcDaemon::Scand);
    msg->setCmd(cmd);
    msg->setSeqNo(seqNo);
    msg->setFlags(IpcProtocol::toFlag(IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(payload.begin(), payload.end()));
    return msg;
}

std::string payloadOf(const IpcMessage& msg)
{
    const auto& p = msg.getPayload();
    return std::string(p.begin(), p.end());
}

IpcFrameView viewOf(const std::vector<std::uint8_t>& v)
{
    return IpcFrameView{v.data(), v.size()};
}

}

// ── Round trip ──────────────────────────────────────────────────────────────────────────

TEST(IpcCodecRoundTrip, PreservesEveryHeaderField)
{
    IpcCodec codec;
    const auto frame = codec.encode(makeMessage("hello", IpcCmd::ApiKeyStateUpdate, 0xDEADBEEF));
    ASSERT_FALSE(frame.empty());

    std::unique_ptr<IpcMessage> out;
    ASSERT_EQ(IpcDecodeResult::Ok, codec.decode(viewOf(frame), out));
    ASSERT_NE(nullptr, out);

    EXPECT_EQ(IpcDaemon::Mgmtd, out->getSrc());
    EXPECT_EQ(IpcDaemon::Scand, out->getDst());
    EXPECT_EQ(IpcCmd::ApiKeyStateUpdate, out->getCmd());
    EXPECT_EQ(0xDEADBEEFu, out->getSeqNo());
    EXPECT_EQ(IPC_PROTOCOL_VERSION, out->getVersion());
    EXPECT_TRUE(out->isRequest());
    EXPECT_EQ("hello", payloadOf(*out));
}

TEST(IpcCodecRoundTrip, HandlesAnEmptyPayload)
{
    IpcCodec codec;
    const auto frame = codec.encode(makeMessage(""));

    ASSERT_EQ(kHeaderLen, frame.size()) << "an empty payload should produce a header-only frame";

    std::unique_ptr<IpcMessage> out;
    ASSERT_EQ(IpcDecodeResult::Ok, codec.decode(viewOf(frame), out));
    ASSERT_NE(nullptr, out);
    EXPECT_TRUE(out->empty());
    EXPECT_EQ(0u, out->getPayloadLen());
}

TEST(IpcCodecRoundTrip, SurvivesBinaryPayloadsIncludingNulBytes)
{
    // Payloads are JSON today, but the codec must not treat the buffer as a C string.
    const std::string binary("a\0b\0\xff\xfe", 6);

    IpcCodec codec;
    const auto frame = codec.encode(makeMessage(binary));

    std::unique_ptr<IpcMessage> out;
    ASSERT_EQ(IpcDecodeResult::Ok, codec.decode(viewOf(frame), out));
    EXPECT_EQ(binary, payloadOf(*out));
    EXPECT_EQ(6u, out->getPayloadLen());
}

TEST(IpcCodecRoundTrip, HandlesALargePayload)
{
    const std::string big(256 * 1024, 'x');

    IpcCodec codec;
    const auto frame = codec.encode(makeMessage(big));
    ASSERT_EQ(kHeaderLen + big.size(), frame.size());

    std::unique_ptr<IpcMessage> out;
    ASSERT_EQ(IpcDecodeResult::Ok, codec.decode(viewOf(frame), out));
    EXPECT_EQ(big.size(), out->getPayloadLen());
    EXPECT_EQ(big, payloadOf(*out));
}

TEST(IpcCodecRoundTrip, EncodedFrameLengthIsHeaderPlusPayload)
{
    IpcCodec codec;

    EXPECT_EQ(kHeaderLen + 0u, codec.encode(makeMessage("")).size());
    EXPECT_EQ(kHeaderLen + 1u, codec.encode(makeMessage("x")).size());
    EXPECT_EQ(kHeaderLen + 100u, codec.encode(makeMessage(std::string(100, 'x'))).size());
}

// ── encode() refusals ───────────────────────────────────────────────────────────────────

TEST(IpcCodecEncode, RefusesANullMessage)
{
    IpcCodec codec;
    std::unique_ptr<IpcMessage> nothing;

    EXPECT_TRUE(codec.encode(nothing).empty());
}

TEST(IpcCodecEncode, RefusesAPayloadLargerThanTheFrameLimit)
{
    // Refusing here is what keeps a peer from being handed a length it will reject anyway.
    const std::string tooBig(IPC_MAX_FRAME_SIZE, 'x');

    IpcCodec codec;
    EXPECT_TRUE(codec.encode(makeMessage(tooBig)).empty());
}

TEST(IpcCodecEncode, AcceptsThePayloadThatExactlyFits)
{
    const std::string exact(IPC_MAX_FRAME_SIZE - kHeaderLen, 'x');

    IpcCodec codec;
    const auto frame = codec.encode(makeMessage(exact));

    ASSERT_FALSE(frame.empty()) << "the largest legal payload must still encode";
    EXPECT_EQ(IPC_MAX_FRAME_SIZE, frame.size());
}

// ── decode() boundaries ─────────────────────────────────────────────────────────────────

TEST(IpcCodecDecode, ReportsNeedMoreDataForAPartialHeader)
{
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));
    frame.resize(kHeaderLen - 1);

    std::unique_ptr<IpcMessage> out;
    EXPECT_EQ(IpcDecodeResult::NeedMoreData, codec.decode(viewOf(frame), out));
    EXPECT_EQ(nullptr, out);
}

TEST(IpcCodecDecode, ReportsNeedMoreDataForAPartialPayload)
{
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello world"));
    frame.resize(frame.size() - 1);

    std::unique_ptr<IpcMessage> out;
    EXPECT_EQ(IpcDecodeResult::NeedMoreData, codec.decode(viewOf(frame), out));
}

TEST(IpcCodecDecode, RejectsTrailingBytesAfterACompleteFrame)
{
    // decode() takes exactly one frame. Extra bytes mean the caller framed it wrong — accepting
    // them would silently swallow the start of the next message.
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));
    frame.push_back(0x00);

    std::unique_ptr<IpcMessage> out;
    EXPECT_EQ(IpcDecodeResult::InvalidFrame, codec.decode(viewOf(frame), out));
}

TEST(IpcCodecDecode, RejectsANullBuffer)
{
    IpcCodec codec;
    std::unique_ptr<IpcMessage> out;

    EXPECT_EQ(IpcDecodeResult::InvalidFrame, codec.decode(IpcFrameView{nullptr, 0}, out));
}

TEST(IpcCodecDecode, RejectsAnUnknownProtocolVersion)
{
    // An older or newer daemon on the same socket: refuse rather than misread its layout.
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));
    frame[0] = IPC_PROTOCOL_VERSION + 1;

    std::unique_ptr<IpcMessage> out;
    EXPECT_EQ(IpcDecodeResult::InvalidFrame, codec.decode(viewOf(frame), out));
}

TEST(IpcCodecDecode, RejectsANonZeroReservedField)
{
    // Reserved must stay zero so it can be given a meaning later without ambiguity.
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));

    IpcWireHeader wire{};
    std::memcpy(&wire, frame.data(), kHeaderLen);
    wire = IpcProtocol::netToHost(wire);
    wire.reserved = 1;
    const IpcWireHeader net = IpcProtocol::hostToNet(wire);
    std::memcpy(frame.data(), &net, kHeaderLen);

    std::unique_ptr<IpcMessage> out;
    EXPECT_EQ(IpcDecodeResult::InvalidFrame, codec.decode(viewOf(frame), out));
}

TEST(IpcCodecDecode, ReportsTooLargeForAnOversizedLengthField)
{
    // A corrupt or hostile length must be refused by size, not by attempting the allocation.
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));

    IpcWireHeader wire{};
    std::memcpy(&wire, frame.data(), kHeaderLen);
    wire = IpcProtocol::netToHost(wire);
    wire.payloadLen = static_cast<std::uint32_t>(IPC_MAX_FRAME_SIZE);
    const IpcWireHeader net = IpcProtocol::hostToNet(wire);
    std::memcpy(frame.data(), &net, kHeaderLen);

    std::unique_ptr<IpcMessage> out;
    EXPECT_EQ(IpcDecodeResult::TooLarge, codec.decode(viewOf(frame), out));
    EXPECT_EQ(nullptr, out);
}

TEST(IpcCodecDecode, ClearsTheOutputParameterOnFailure)
{
    // The caller may reuse the same unique_ptr across reads; a rejected frame must not leave
    // the previous message in it and look like a success.
    IpcCodec codec;
    std::unique_ptr<IpcMessage> out = makeMessage("stale");

    auto frame = codec.encode(makeMessage("hello"));
    frame[0] = IPC_PROTOCOL_VERSION + 1;

    EXPECT_EQ(IpcDecodeResult::InvalidFrame, codec.decode(viewOf(frame), out));
    EXPECT_EQ(nullptr, out);
}

// ── peekFrameSize() ─────────────────────────────────────────────────────────────────────
// The read loop uses this to know how many bytes to wait for before calling decode().

TEST(IpcCodecPeek, ReportsTheFullFrameLength)
{
    IpcCodec codec;
    const auto frame = codec.encode(makeMessage("hello"));

    std::size_t size = 0;
    ASSERT_EQ(IpcPeekResult::Ok, codec.peekFrameSize(frame.data(), frame.size(), size));
    EXPECT_EQ(frame.size(), size);
}

TEST(IpcCodecPeek, WorksFromTheHeaderAloneBeforeThePayloadArrives)
{
    // The whole point: the length is known once 16 bytes are in hand.
    IpcCodec codec;
    const auto frame = codec.encode(makeMessage(std::string(500, 'x')));

    std::size_t size = 0;
    ASSERT_EQ(IpcPeekResult::Ok, codec.peekFrameSize(frame.data(), kHeaderLen, size));
    EXPECT_EQ(kHeaderLen + 500u, size);
}

TEST(IpcCodecPeek, ReportsNeedMoreDataBelowAFullHeader)
{
    IpcCodec codec;
    const auto frame = codec.encode(makeMessage("hello"));

    std::size_t size = 123;
    EXPECT_EQ(IpcPeekResult::NeedMoreData, codec.peekFrameSize(frame.data(), kHeaderLen - 1, size));
    EXPECT_EQ(0u, size) << "the out parameter must be reset, not left holding a stale length";
}

TEST(IpcCodecPeek, ReportsNeedMoreDataForANullBuffer)
{
    IpcCodec codec;
    std::size_t size = 0;

    EXPECT_EQ(IpcPeekResult::NeedMoreData, codec.peekFrameSize(nullptr, 0, size));
}

TEST(IpcCodecPeek, RejectsAnUnknownVersion)
{
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));
    frame[0] = 0xFF;

    std::size_t size = 0;
    EXPECT_EQ(IpcPeekResult::InvalidFrame, codec.peekFrameSize(frame.data(), frame.size(), size));
    EXPECT_EQ(0u, size);
}

TEST(IpcCodecPeek, RejectsAnOversizedLengthField)
{
    // peek must refuse this too — otherwise the read loop would sit waiting for bytes that
    // decode() was always going to reject.
    IpcCodec codec;
    auto frame = codec.encode(makeMessage("hello"));

    IpcWireHeader wire{};
    std::memcpy(&wire, frame.data(), kHeaderLen);
    wire = IpcProtocol::netToHost(wire);
    wire.payloadLen = 0xFFFFFFFFu;
    const IpcWireHeader net = IpcProtocol::hostToNet(wire);
    std::memcpy(frame.data(), &net, kHeaderLen);

    std::size_t size = 0;
    EXPECT_EQ(IpcPeekResult::InvalidFrame, codec.peekFrameSize(frame.data(), frame.size(), size));
}

// ── Streaming ───────────────────────────────────────────────────────────────────────────

TEST(IpcCodecStreaming, TwoConcatenatedFramesAreSplitByPeekAndDecodedInOrder)
{
    // The realistic case: a socket read returns several messages at once.
    IpcCodec codec;
    const auto first = codec.encode(makeMessage("first", IpcCmd::ApiKeyStateUpdate, 1));
    const auto second = codec.encode(makeMessage("second", IpcCmd::ApiConnectorTestResponse, 2));

    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), first.begin(), first.end());
    stream.insert(stream.end(), second.begin(), second.end());

    std::size_t offset = 0;
    std::vector<std::string> payloads;
    std::vector<std::uint32_t> seqNos;

    while (offset < stream.size())
    {
        std::size_t frameSize = 0;
        ASSERT_EQ(IpcPeekResult::Ok, codec.peekFrameSize(stream.data() + offset, stream.size() - offset, frameSize));
        ASSERT_GT(frameSize, 0u);

        std::unique_ptr<IpcMessage> out;
        ASSERT_EQ(IpcDecodeResult::Ok, codec.decode(IpcFrameView{stream.data() + offset, frameSize}, out));
        ASSERT_NE(nullptr, out);

        payloads.push_back(payloadOf(*out));
        seqNos.push_back(out->getSeqNo());
        offset += frameSize;
    }

    ASSERT_EQ(2u, payloads.size());
    EXPECT_EQ("first", payloads[0]);
    EXPECT_EQ("second", payloads[1]);
    EXPECT_EQ(1u, seqNos[0]);
    EXPECT_EQ(2u, seqNos[1]);
    EXPECT_EQ(stream.size(), offset) << "the stream must be consumed exactly";
}
