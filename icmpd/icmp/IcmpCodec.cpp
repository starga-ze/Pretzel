#include "icmp/IcmpCodec.h"

namespace nf::icmpd
{

std::vector<std::uint8_t> IcmpCodec::encode(const IcmpPacket& packet) const
{
    return packet.serialize();
}

IcmpDecodeResult IcmpCodec::decode(const std::vector<std::uint8_t>& bytes,
                                   std::unique_ptr<IcmpPacket>& outPacket) const
{
    outPacket.reset();

    if (bytes.empty())
        return IcmpDecodeResult::Empty;

    outPacket = IcmpPacket::parse(bytes);
    if (!outPacket)
        return IcmpDecodeResult::Invalid;

    return IcmpDecodeResult::Ok;
}

} // namespace nf::icmpd
