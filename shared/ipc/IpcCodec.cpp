#include "ipc/IpcCodec.h"

#include <arpa/inet.h>
#include <cstring>

namespace nf::ipc
{

std::vector<std::uint8_t> IpcCodec::encode(const IpcMessage& msg) const
{
    const std::size_t payloadLen = msg.payload.size();
    const std::size_t bodyLen = sizeof(IpcWireHeader) + payloadLen;
    const std::size_t frameLen = sizeof(std::uint16_t) + bodyLen;

    if (bodyLen > 0xFFFF || frameLen > IPC_MAX_FRAME_SIZE)
        return {};

    const std::uint16_t bodyLenNet = htons(static_cast<std::uint16_t>(bodyLen));

    std::vector<std::uint8_t> out(frameLen);

    std::memcpy(out.data(), &bodyLenNet, sizeof(bodyLenNet));

    IpcWireHeader wire {};
    wire.version = msg.version;
    wire.src = static_cast<std::uint8_t>(msg.src);
    wire.dst = static_cast<std::uint8_t>(msg.dst);
    wire.flags = msg.flags;
    wire.cmd = static_cast<std::uint16_t>(msg.cmd);
    wire.reserved = 0;
    wire.seqNo = msg.seqNo;
    wire.payloadLen = static_cast<std::uint32_t>(payloadLen);

    const IpcWireHeader wireNet = IpcProtocol::hostToNet(wire);

    std::memcpy(out.data() + sizeof(std::uint16_t), &wireNet, sizeof(wireNet));

    if (payloadLen > 0)
    {
        std::memcpy(
            out.data() + sizeof(std::uint16_t) + sizeof(IpcWireHeader),
            msg.payload.data(),
            payloadLen);
    }

    return out;
}

IpcDecodeResult IpcCodec::decode(const std::vector<std::uint8_t>& frame, IpcMessage& out) const
{
    if (frame.size() < sizeof(std::uint16_t) + sizeof(IpcWireHeader))
        return IpcDecodeResult::NeedMoreData;

    std::uint16_t bodyLenNet = 0;
    std::memcpy(&bodyLenNet, frame.data(), sizeof(bodyLenNet));

    const std::uint16_t bodyLen = ntohs(bodyLenNet);
    const std::size_t totalLen = sizeof(std::uint16_t) + bodyLen;

    if (totalLen > IPC_MAX_FRAME_SIZE)
        return IpcDecodeResult::TooLarge;

    if (frame.size() < totalLen)
        return IpcDecodeResult::NeedMoreData;

    if (frame.size() != totalLen)
        return IpcDecodeResult::InvalidFrame;

    IpcWireHeader wireNet {};
    std::memcpy(
        &wireNet,
        frame.data() + sizeof(std::uint16_t),
        sizeof(IpcWireHeader));

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return IpcDecodeResult::InvalidFrame;

    if (wire.reserved != 0)
        return IpcDecodeResult::InvalidFrame;

    if (bodyLen < sizeof(IpcWireHeader))
        return IpcDecodeResult::InvalidFrame;

    const std::size_t payloadLen = bodyLen - sizeof(IpcWireHeader);
    if (wire.payloadLen != payloadLen)
        return IpcDecodeResult::InvalidFrame;

    out.version = wire.version;
    out.src = static_cast<IpcDaemon>(wire.src);
    out.dst = static_cast<IpcDaemon>(wire.dst);
    out.flags = wire.flags;
    out.cmd = static_cast<IpcCmd>(wire.cmd);
    out.seqNo = wire.seqNo;
    out.payload.resize(payloadLen);

    if (payloadLen > 0)
    {
        std::memcpy(
            out.payload.data(),
            frame.data() + sizeof(std::uint16_t) + sizeof(IpcWireHeader),
            payloadLen);
    }

    return IpcDecodeResult::Ok;
}

std::size_t IpcCodec::peekFrameSize(const std::uint8_t* data, std::size_t len) const
{
    if (!data || len < sizeof(std::uint16_t))
        return 0;

    std::uint16_t bodyLenNet = 0;
    std::memcpy(&bodyLenNet, data, sizeof(bodyLenNet));

    const std::uint16_t bodyLen = ntohs(bodyLenNet);
    return sizeof(std::uint16_t) + bodyLen;
}

} // namespace nf::ipc
