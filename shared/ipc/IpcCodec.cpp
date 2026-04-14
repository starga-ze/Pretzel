#include "ipc/IpcCodec.h"

#include <cstring>

namespace nf::ipc
{

std::vector<std::uint8_t> IpcCodec::encode(const IpcMessage& msg) const
{
    const std::size_t payloadLen = msg.getPayloadLen();
    const std::size_t frameLen = sizeof(IpcWireHeader) + payloadLen;

    if (frameLen > IPC_MAX_FRAME_SIZE)
        return {};

    std::vector<std::uint8_t> out(frameLen);

    IpcWireHeader wire = msg.toWireHeader();
    IpcWireHeader wireNet = IpcProtocol::hostToNet(wire);

    std::memcpy(out.data(), &wireNet, sizeof(wireNet));

    if (payloadLen > 0)
    {
        std::memcpy(out.data() + sizeof(IpcWireHeader),
                    msg.getPayload().data(),
                    payloadLen);
    }

    return out;
}

IpcDecodeResult IpcCodec::decode(const std::vector<std::uint8_t>& frame, IpcMessage& out) const
{
    if (frame.size() < sizeof(IpcWireHeader))
        return IpcDecodeResult::NeedMoreData;

    IpcWireHeader wireNet {};
    std::memcpy(&wireNet, frame.data(), sizeof(IpcWireHeader));

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return IpcDecodeResult::InvalidFrame;

    if (wire.reserved != 0)
        return IpcDecodeResult::InvalidFrame;

    const std::size_t totalLen = sizeof(IpcWireHeader) + wire.payloadLen;

    if (totalLen > IPC_MAX_FRAME_SIZE)
        return IpcDecodeResult::TooLarge;

    if (frame.size() < totalLen)
        return IpcDecodeResult::NeedMoreData;

    if (frame.size() != totalLen)
        return IpcDecodeResult::InvalidFrame;

    std::vector<std::uint8_t> payload(wire.payloadLen);
    if (wire.payloadLen > 0)
    {
        std::memcpy(payload.data(),
                    frame.data() + sizeof(IpcWireHeader),
                    wire.payloadLen);
    }

    IpcHeader header = IpcHeader::build(
        static_cast<IpcDaemon>(wire.src),
        static_cast<IpcDaemon>(wire.dst),
        static_cast<IpcCmd>(wire.cmd),
        wire.seqNo,
        wire.flags);

    header.version = wire.version;

    out = IpcMessage(std::move(header), std::move(payload));
    return IpcDecodeResult::Ok;
}

std::size_t IpcCodec::peekFrameSize(const std::uint8_t* data, std::size_t len) const
{
    if (!data || len < sizeof(IpcWireHeader))
        return 0;

    IpcWireHeader wireNet {};
    std::memcpy(&wireNet, data, sizeof(IpcWireHeader));

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return 0;

    if (wire.reserved != 0)
        return 0;

    const std::size_t frameSize = sizeof(IpcWireHeader) + wire.payloadLen;

    if (frameSize > IPC_MAX_FRAME_SIZE)
        return 0;

    return frameSize;
}

} // namespace nf::ipc
