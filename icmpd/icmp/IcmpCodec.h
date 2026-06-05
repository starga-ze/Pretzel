#pragma once

#include "icmp/IcmpPacket.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace nf::icmpd
{

struct IcmpFrameView
{
    const std::uint8_t* data{nullptr};
    std::size_t size{0};

    bool empty() const noexcept
    {
        return data == nullptr || size == 0;
    }
};

enum class IcmpDecodeResult
{
    Ok,
    Empty,
    InvalidFrame,
};

class IcmpCodec final
{
public:
    static constexpr std::size_t ICMP_ECHO_HEADER_LEN = 8;

    std::vector<std::uint8_t> encode(const std::unique_ptr<IcmpPacket>& packet) const;

    std::vector<std::uint8_t> encode(const IcmpPacket& packet) const;

    IcmpDecodeResult decode(IcmpFrameView frame, std::unique_ptr<IcmpPacket>& out) const;

private:
    static std::uint16_t calculateChecksum(const std::uint8_t* data, std::size_t len);

    static std::uint16_t readU16Be(const std::uint8_t* data);

    static void writeU16Be(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint16_t value);

    static std::size_t icmpOffset(IcmpFrameView frame);
};

} // namespace nf::icmpd
