#include "icmp/IcmpPacket.h"

#include <arpa/inet.h>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace nf::icmpd
{

namespace
{

constexpr std::size_t kIcmpEchoHeaderLen = 8;

void writeU16Be(std::vector<std::uint8_t>& buf,
                std::size_t offset,
                std::uint16_t value)
{
    const auto net = htons(value);
    std::memcpy(buf.data() + offset, &net, sizeof(net));
}

std::uint16_t readU16Be(const std::vector<std::uint8_t>& buf,
                        std::size_t offset)
{
    std::uint16_t value = 0;
    std::memcpy(&value, buf.data() + offset, sizeof(value));
    return ntohs(value);
}

const char* icmpTypeToStr(IcmpType type)
{
    switch (type)
    {
    case IcmpType::EchoReply:
        return "EchoReply";

    case IcmpType::EchoRequest:
        return "EchoRequest";

    default:
        return "Unknown";
    }
}

}

IcmpPacket::IcmpPacket()
    : m_type(IcmpType::EchoRequest),
      m_code(IcmpCode::Echo),
      m_identifier(0),
      m_sequence(0),
      m_payload()
{
}

IcmpPacket::IcmpPacket(IcmpType type,
                       IcmpCode code,
                       std::uint16_t identifier,
                       std::uint16_t sequence,
                       std::vector<std::uint8_t> payload)
    : m_type(type),
      m_code(code),
      m_identifier(identifier),
      m_sequence(sequence),
      m_payload(std::move(payload))
{
}

IcmpPacket IcmpPacket::buildEchoRequest(std::uint16_t identifier,
                                        std::uint16_t sequence,
                                        std::vector<std::uint8_t> payload)
{
    return IcmpPacket(
        IcmpType::EchoRequest,
        IcmpCode::Echo,
        identifier,
        sequence,
        std::move(payload));
}

IcmpType IcmpPacket::type() const
{
    return m_type;
}

void IcmpPacket::setType(IcmpType type)
{
    m_type = type;
}

IcmpCode IcmpPacket::code() const
{
    return m_code;
}

void IcmpPacket::setCode(IcmpCode code)
{
    m_code = code;
}

std::uint16_t IcmpPacket::checksum() const
{
    const auto wire = toWire();
    return readU16Be(wire, 2);
}

std::uint16_t IcmpPacket::identifier() const
{
    return m_identifier;
}

void IcmpPacket::setIdentifier(std::uint16_t identifier)
{
    m_identifier = identifier;
}

std::uint16_t IcmpPacket::sequence() const
{
    return m_sequence;
}

void IcmpPacket::setSequence(std::uint16_t sequence)
{
    m_sequence = sequence;
}

const std::vector<std::uint8_t>& IcmpPacket::payload() const
{
    return m_payload;
}

std::vector<std::uint8_t>& IcmpPacket::payload()
{
    return m_payload;
}

void IcmpPacket::setPayload(const std::vector<std::uint8_t>& payload)
{
    m_payload = payload;
}

void IcmpPacket::setPayload(std::vector<std::uint8_t>&& payload)
{
    m_payload = std::move(payload);
}

void IcmpPacket::setPayload(const void* data, std::size_t len)
{
    if (data == nullptr && len != 0)
    {
        throw std::invalid_argument("IcmpPacket::setPayload null data with non-zero length");
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_payload.assign(bytes, bytes + len);
}

std::size_t IcmpPacket::payloadLen() const
{
    return m_payload.size();
}

std::size_t IcmpPacket::wireLen() const
{
    return kIcmpEchoHeaderLen + m_payload.size();
}

bool IcmpPacket::empty() const
{
    return m_payload.empty();
}

std::vector<std::uint8_t> IcmpPacket::toWire() const
{
    std::vector<std::uint8_t> wire(kIcmpEchoHeaderLen + m_payload.size(), 0);

    wire[0] = static_cast<std::uint8_t>(m_type);
    wire[1] = static_cast<std::uint8_t>(m_code);

    // checksum field initially zero
    wire[2] = 0;
    wire[3] = 0;

    writeU16Be(wire, 4, m_identifier);
    writeU16Be(wire, 6, m_sequence);

    if (!m_payload.empty())
    {
        std::memcpy(wire.data() + kIcmpEchoHeaderLen,
                    m_payload.data(),
                    m_payload.size());
    }

    const auto csum = calculateChecksum(wire.data(), wire.size());
    writeU16Be(wire, 2, csum);

    return wire;
}

std::uint16_t IcmpPacket::calculateChecksum(const std::uint8_t* data,
                                            std::size_t len)
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
    {
        sum += static_cast<std::uint16_t>(*data) << 8;
    }

    while ((sum >> 16) != 0)
    {
        sum = (sum & 0xffffU) + (sum >> 16);
    }

    return static_cast<std::uint16_t>(~sum & 0xffffU);
}

std::string IcmpPacket::dump() const
{
    const auto wire = toWire();

    std::ostringstream oss;

    oss << "Type      : " << icmpTypeToStr(m_type)
        << " (" << static_cast<int>(m_type) << ")\n";

    oss << "Code      : " << static_cast<int>(m_code) << "\n";
    oss << "Checksum  : 0x"
        << std::hex << std::setw(4) << std::setfill('0')
        << checksum()
        << std::dec << "\n";

    oss << "Identifier: " << m_identifier << "\n";
    oss << "Sequence  : " << m_sequence << "\n";
    oss << "Payload   : " << m_payload.size() << " bytes\n";
    oss << "Wire      : " << wire.size() << " bytes";

    if (!wire.empty())
    {
        oss << "\n  ";

        for (std::size_t i = 0; i < wire.size(); ++i)
        {
            oss << std::hex
                << std::setw(2)
                << std::setfill('0')
                << static_cast<int>(wire[i]);

            if (i + 1 != wire.size())
            {
                oss << ' ';
            }
        }

        oss << std::dec;
    }

    return oss.str();
}

}
