#include "icmp/IcmpPacket.h"

#include "icmp/IcmpCodec.h"
#include "icmp/IcmpProtocol.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace pz::icmpd
{

IcmpHeader::IcmpHeader(IcmpType type, IcmpCode code, std::uint16_t identifier, std::uint16_t sequence)
    : m_type(type), m_code(code), m_identifier(identifier), m_sequence(sequence)
{
}

IcmpHeader IcmpHeader::build(IcmpType type, IcmpCode code, std::uint16_t identifier, std::uint16_t sequence)
{
    return IcmpHeader(type, code, identifier, sequence);
}

IcmpHeader IcmpHeader::buildEchoRequest(std::uint16_t identifier, std::uint16_t sequence)
{
    return IcmpHeader(IcmpType::EchoRequest, IcmpCode::Echo, identifier, sequence);
}

IcmpType IcmpHeader::type() const
{
    return m_type;
}

void IcmpHeader::setType(IcmpType type)
{
    m_type = type;
}

IcmpCode IcmpHeader::code() const
{
    return m_code;
}

void IcmpHeader::setCode(IcmpCode code)
{
    m_code = code;
}

std::uint16_t IcmpHeader::checksum() const
{
    return m_checksum;
}

void IcmpHeader::setChecksum(std::uint16_t checksum)
{
    m_checksum = checksum;
}

std::uint16_t IcmpHeader::identifier() const
{
    return m_identifier;
}

void IcmpHeader::setIdentifier(std::uint16_t identifier)
{
    m_identifier = identifier;
}

std::uint16_t IcmpHeader::sequence() const
{
    return m_sequence;
}

void IcmpHeader::setSequence(std::uint16_t sequence)
{
    m_sequence = sequence;
}

IcmpPacket::IcmpPacket() = default;

IcmpPacket::IcmpPacket(IcmpHeader header) : m_header(std::move(header))
{
}

IcmpPacket::IcmpPacket(IcmpHeader header, std::vector<std::uint8_t> payload)
    : m_header(std::move(header)), m_payload(std::move(payload))
{
}

const IcmpHeader& IcmpPacket::header() const
{
    return m_header;
}

IcmpHeader& IcmpPacket::header()
{
    return m_header;
}

IcmpType IcmpPacket::type() const
{
    return m_header.type();
}

IcmpCode IcmpPacket::code() const
{
    return m_header.code();
}

std::uint16_t IcmpPacket::checksum() const
{
    return m_header.checksum();
}

std::uint16_t IcmpPacket::identifier() const
{
    return m_header.identifier();
}

std::uint16_t IcmpPacket::sequence() const
{
    return m_header.sequence();
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
        throw std::invalid_argument("IcmpPacket::setPayload null data with non-zero length");

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_payload.assign(bytes, bytes + len);
}

std::size_t IcmpPacket::payloadLen() const
{
    return m_payload.size();
}

bool IcmpPacket::empty() const
{
    return m_payload.empty();
}

std::string IcmpPacket::dump() const
{
    std::ostringstream oss;

    oss << "Type      : " << IcmpProtocol::typeToStr(m_header.type()) << " (" << static_cast<int>(m_header.type())
        << ")\n";

    oss << "Code      : " << IcmpProtocol::codeToStr(m_header.type(), m_header.code()) << " ("
        << static_cast<int>(m_header.code()) << ")\n";

    oss << "Identifier: " << m_header.identifier() << "\n";

    oss << "Sequence  : " << m_header.sequence() << "\n";

    oss << "Payload   : " << m_payload.size() << " bytes";

    if (!m_payload.empty())
    {
        oss << "\n  ";

        for (std::size_t i = 0; i < m_payload.size(); ++i)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(m_payload[i]);

            if (i + 1 != m_payload.size())
                oss << ' ';
        }

        oss << std::dec;
    }

    return oss.str();
}

}
