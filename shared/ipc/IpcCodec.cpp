#include "ipc/IpcCodec.h"

#include <cstring>

namespace nf::ipc
{

std::vector<std::uint8_t> IpcCodec::encode(const IpcMessage& msg) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    const std::size_t payloadLen = msg.getPayloadLen();
    if (payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
        return {};

    const std::size_t frameLen = headerLen + payloadLen;

    std::vector<std::uint8_t> out(frameLen);

    IpcWireHeader wire = msg.toWireHeader();
    IpcWireHeader wireNet = IpcProtocol::hostToNet(wire);

    std::memcpy(out.data(), &wireNet, headerLen);

    if (payloadLen > 0)
    {
        std::memcpy(out.data() + headerLen, msg.getPayload().data(), payloadLen);
    }

    return out;
}

IpcDecodeResult IpcCodec::decode(const std::vector<std::uint8_t>& frame, IpcMessage& out) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    if (frame.size() < headerLen)
        return IpcDecodeResult::NeedMoreData;

    IpcWireHeader wireNet{};
    std::memcpy(&wireNet, frame.data(), headerLen);

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return IpcDecodeResult::InvalidFrame;

    if (wire.reserved != 0)
        return IpcDecodeResult::InvalidFrame;

    if (wire.payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
        return IpcDecodeResult::TooLarge;

    const std::size_t totalLen = headerLen + wire.payloadLen;

    if (frame.size() < totalLen)
        return IpcDecodeResult::NeedMoreData;

    if (frame.size() != totalLen)
        return IpcDecodeResult::InvalidFrame;

    IpcHeader header = IpcHeader::build(static_cast<IpcDaemon>(wire.src), static_cast<IpcDaemon>(wire.dst),
                                        static_cast<IpcCmd>(wire.cmd), wire.seqNo, wire.flags);

    header.version = wire.version;

    std::vector<std::uint8_t> payload;
    if (wire.payloadLen > 0)
    {
        payload.resize(wire.payloadLen);
        std::memcpy(payload.data(), frame.data() + headerLen, wire.payloadLen);
    }

    out = IpcMessage(std::move(header), std::move(payload));
    return IpcDecodeResult::Ok;
}

IpcPeekResult IpcCodec::peekFrameSize(const std::uint8_t* data, 
        std::size_t len, std::size_t& outFrameSize) const
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
