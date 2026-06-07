#include "ipc/IpcCodec.h"

#include <cstring>

namespace pz::ipc
{

std::vector<std::uint8_t>
IpcCodec::encode(const std::unique_ptr<IpcMessage>& msg) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    if (!msg)
        return {};

    const std::size_t payloadLen = msg->getPayloadLen();

    if (payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
        return {};

    const std::size_t frameLen = headerLen + payloadLen;

    std::vector<std::uint8_t> frame;
    frame.resize(frameLen);

    const IpcWireHeader wireHost = msg->toWireHeader();
    const IpcWireHeader wireNet = IpcProtocol::hostToNet(wireHost);

    std::memcpy(frame.data(), &wireNet, headerLen);

    if (payloadLen > 0)
    {
        std::memcpy(frame.data() + headerLen,
                    msg->getPayload().data(),
                    payloadLen);
    }

    return frame;
}

IpcDecodeResult 
IpcCodec::decode(IpcFrameView frame, std::unique_ptr<IpcMessage>& out) const
{
    constexpr std::size_t headerLen = sizeof(IpcWireHeader);

    out.reset();

    if (!frame.data)
        return IpcDecodeResult::InvalidFrame;

    if (frame.size < headerLen)
        return IpcDecodeResult::NeedMoreData;

    IpcWireHeader wireNet {};
    std::memcpy(&wireNet, frame.data, headerLen);

    const IpcWireHeader wire = IpcProtocol::netToHost(wireNet);

    if (wire.version != IPC_PROTOCOL_VERSION)
        return IpcDecodeResult::InvalidFrame;

    if (wire.reserved != 0)
        return IpcDecodeResult::InvalidFrame;

    if (wire.payloadLen > IPC_MAX_FRAME_SIZE - headerLen)
        return IpcDecodeResult::TooLarge;

    const std::size_t totalLen = headerLen + wire.payloadLen;

    if (frame.size < totalLen)
        return IpcDecodeResult::NeedMoreData;

    if (frame.size != totalLen)
        return IpcDecodeResult::InvalidFrame;

    const auto src = static_cast<IpcDaemon>(wire.src);
    const auto dst = static_cast<IpcDaemon>(wire.dst);
    const auto cmd = static_cast<IpcCmd>(wire.cmd);

    IpcHeader header = IpcHeader::build(
        src,
        dst,
        cmd,
        wire.seqNo,
        wire.flags
    );

    header.version = wire.version;

    std::vector<std::uint8_t> payload;
    if (wire.payloadLen > 0)
    {
        const std::uint8_t* payloadBegin = frame.data + headerLen;
        const std::uint8_t* payloadEnd = payloadBegin + wire.payloadLen;

        payload.assign(payloadBegin, payloadEnd);
    }

    out = std::make_unique<IpcMessage>(
        std::move(header),
        std::move(payload)
    );

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

} // namespace pz::ipc
