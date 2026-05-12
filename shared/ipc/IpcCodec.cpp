#include "ipc/IpcCodec.h"

#include <cstring>

namespace nf::ipc
{

std::vector<std::uint8_t> IpcCodec::encode(const IpcMessage& msg) const
{
    std::vector<std::uint8_t> out;

    if (!encode(msg, out))
        return {};

    return out;
}

std::vector<std::uint8_t> IpcCodec::encode(const std::unique_ptr<IpcMessage>& msg) const
{
    if (!msg)
        return {};

    return encode(*msg);
}

bool IpcCodec::encode(const std::unique_ptr<IpcMessage>& msg, std::vector<std::uint8_t>& out) const
{
    if (!msg)
    {
        out.clear();
        return false;
    }

    return encode(*msg, out);
}

bool IpcCodec::encode(const IpcMessage& msg, std::vector<std::uint8_t>& out) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    const std::size_t payloadLen = msg.getPayloadLen();
    if (payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
    {
        out.clear();
        return false;
    }

    const std::size_t frameLen = headerLen + payloadLen;

    out.clear();
    out.resize(frameLen);

    const IpcWireHeader wire = msg.toWireHeader();
    const IpcWireHeader wireNet = IpcProtocol::hostToNet(wire);

    std::memcpy(out.data(), &wireNet, headerLen);

    if (payloadLen > 0)
    {
        std::memcpy(out.data() + headerLen, msg.getPayload().data(), payloadLen);
    }

    return true;
}

IpcDecodeResult IpcCodec::decode(const std::vector<std::uint8_t>& frame, IpcMessage& out) const
{
    return decode(frame.data(), frame.size(), out);
}

IpcDecodeResult IpcCodec::decode(const std::vector<std::uint8_t>& frame, std::unique_ptr<IpcMessage>& out) const
{
    return decode(frame.data(), frame.size(), out);
}

IpcDecodeResult IpcCodec::decode(const std::uint8_t* data, std::size_t len, std::unique_ptr<IpcMessage>& out) const
{
    out.reset();

    auto msg = std::make_unique<IpcMessage>();

    const IpcDecodeResult rc = decode(data, len, *msg);
    if (rc != IpcDecodeResult::Ok)
        return rc;

    out = std::move(msg);
    return IpcDecodeResult::Ok;
}

IpcDecodeResult IpcCodec::decode(const std::uint8_t* data, std::size_t len, IpcMessage& out) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    if (!data || len < headerLen)
        return IpcDecodeResult::NeedMoreData;

    IpcWireHeader wireNet{};
    std::memcpy(&wireNet, data, headerLen);

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return IpcDecodeResult::InvalidFrame;

    if (wire.reserved != 0)
        return IpcDecodeResult::InvalidFrame;

    if (wire.payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
        return IpcDecodeResult::TooLarge;

    const std::size_t totalLen = headerLen + wire.payloadLen;

    if (len < totalLen)
        return IpcDecodeResult::NeedMoreData;

    if (len != totalLen)
        return IpcDecodeResult::InvalidFrame;

    IpcHeader header = IpcHeader::build(static_cast<IpcDaemon>(wire.src), static_cast<IpcDaemon>(wire.dst),
                                        static_cast<IpcCmd>(wire.cmd), wire.seqNo, wire.flags);

    header.version = wire.version;

    std::vector<std::uint8_t> payload;
    if (wire.payloadLen > 0)
    {
        const std::uint8_t* payloadBegin = data + headerLen;
        const std::uint8_t* payloadEnd = payloadBegin + wire.payloadLen;

        payload.assign(payloadBegin, payloadEnd);
    }

    out = IpcMessage(std::move(header), std::move(payload));

    return IpcDecodeResult::Ok;
}

IpcPeekResult IpcCodec::peekFrameSize(const std::uint8_t* data, std::size_t len, std::size_t& outFrameSize) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    outFrameSize = 0;

    if (!data || len < headerLen)
        return IpcPeekResult::NeedMoreData;

    IpcWireHeader wireNet{};
    std::memcpy(&wireNet, data, headerLen);

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return IpcPeekResult::InvalidFrame;

    if (wire.reserved != 0)
        return IpcPeekResult::InvalidFrame;

    if (wire.payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
        return IpcPeekResult::InvalidFrame;

    outFrameSize = headerLen + wire.payloadLen;

    return IpcPeekResult::Ok;
}

} // namespace nf::ipc
