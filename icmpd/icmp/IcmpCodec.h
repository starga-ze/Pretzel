#pragma once

#include "icmp/IcmpPacket.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nf::icmpd
{

enum class IcmpDecodeResult
{
    Ok,
    Empty,
    Invalid,
};

class IcmpCodec final
{
public:
    std::vector<std::uint8_t> encode(const IcmpPacket& packet) const;

    IcmpDecodeResult decode(const std::vector<std::uint8_t>& bytes,
                            std::unique_ptr<IcmpPacket>& outPacket) const;
};

} // namespace nf::icmpd
