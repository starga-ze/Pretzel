#include "icmp/IcmpCodec.h"

#include <arpa/inet.h>

#include <cstring>
#include <utility>

namespace nf::icmpd
{

static constexpr std::size_t kIpv4MinHeaderLen = 20;
static constexpr std::size_t kInvalidIcmpOffset = static_cast<std::size_t>(-1);

std::vector<std::uint8_t> IcmpCodec::encode(const std::unique_ptr<IcmpPacket>& packet) const
{
    if (!packet)
        return {};

    return encode(*packet);
}

std::vector<std::uint8_t> IcmpCodec::encode(const IcmpPacket& packet) const
{
    std::vector<std::uint8_t> frame(ICMP_ECHO_HEADER_LEN + packet.payloadLen(), 0);

    frame[0] = static_cast<std::uint8_t>(packet.header().type());
    frame[1] = static_cast<std::uint8_t>(packet.header().code());

    writeU16Be(frame, 2, 0);
    writeU16Be(frame, 4, packet.header().identifier());
    writeU16Be(frame, 6, packet.header().sequence());

    if (!packet.payload().empty())
    {
        std::memcpy(frame.data() + ICMP_ECHO_HEADER_LEN, packet.payload().data(), packet.payload().size());
    }

    const auto checksum = calculateChecksum(frame.data(), frame.size());
    writeU16Be(frame, 2, checksum);

    return frame;
}

IcmpDecodeResult IcmpCodec::decode(IcmpFrameView frame, std::unique_ptr<IcmpPacket>& out) const
{
    out.reset();

    if (frame.empty())
        return IcmpDecodeResult::Empty;

    const std::size_t offset = icmpOffset(frame);
    if (offset == kInvalidIcmpOffset)
        return IcmpDecodeResult::InvalidFrame;

    if (frame.size - offset < ICMP_ECHO_HEADER_LEN)
        return IcmpDecodeResult::InvalidFrame;

    const std::uint8_t* icmp = frame.data + offset;

    IcmpHeader header(static_cast<IcmpType>(icmp[0]), static_cast<IcmpCode>(icmp[1]), 
            readU16Be(icmp + 4), readU16Be(icmp + 6));

    header.setChecksum(readU16Be(icmp + 2));

    std::vector<std::uint8_t> payload;

    const std::size_t payloadLen = frame.size - offset - ICMP_ECHO_HEADER_LEN;
    if (payloadLen > 0)
    {
        const std::uint8_t* payloadBegin = icmp + ICMP_ECHO_HEADER_LEN;
        const std::uint8_t* payloadEnd = payloadBegin + payloadLen;

        payload.assign(payloadBegin, payloadEnd);
    }

    out = std::make_unique<IcmpPacket>(std::move(header), std::move(payload));

    return IcmpDecodeResult::Ok;
}

std::uint16_t IcmpCodec::calculateChecksum(const std::uint8_t* data, std::size_t len)
{
    std::uint32_t sum = 0;

    while (len > 1)
    {
        std::uint16_t word = 0;
        std::memcpy(&word, data, sizeof(word));

        sum += ntohs(word);

        data += 2;
        len -= 2;
    }

    if (len == 1)
        sum += static_cast<std::uint16_t>(*data) << 8;

    while ((sum >> 16) != 0)
        sum = (sum & 0xffffU) + (sum >> 16);

    return static_cast<std::uint16_t>(~sum & 0xffffU);
}

std::uint16_t IcmpCodec::readU16Be(const std::uint8_t* data)
{
    std::uint16_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return ntohs(value);
}

void IcmpCodec::writeU16Be(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint16_t value)
{
    const auto net = htons(value);
    std::memcpy(buf.data() + offset, &net, sizeof(net));
}

std::size_t IcmpCodec::icmpOffset(IcmpFrameView frame)
{
    if (frame.empty())
        return kInvalidIcmpOffset;

    if (frame.size < ICMP_ECHO_HEADER_LEN)
        return kInvalidIcmpOffset;

    const std::uint8_t version = frame.data[0] >> 4;

    if (version == 4)
    {
        if (frame.size < kIpv4MinHeaderLen)
            return kInvalidIcmpOffset;

        const std::size_t ipHeaderLen = static_cast<std::size_t>(frame.data[0] & 0x0fU) * 4U;

        if (ipHeaderLen < kIpv4MinHeaderLen)
            return kInvalidIcmpOffset;

        if (ipHeaderLen > frame.size)
            return kInvalidIcmpOffset;

        if (frame.size - ipHeaderLen < ICMP_ECHO_HEADER_LEN)
            return kInvalidIcmpOffset;

        return ipHeaderLen;
    }

    return 0;
}

} // namespace nf::icmpd
