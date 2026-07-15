#pragma once

#include "icmp/IcmpProtocol.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pz::icmpd
{

class IcmpHeader final
{
public:
    IcmpHeader() = default;

    IcmpHeader(IcmpType type, IcmpCode code, std::uint16_t identifier, std::uint16_t sequence);

    static IcmpHeader build(IcmpType type, IcmpCode code, std::uint16_t identifier, std::uint16_t sequence);

    static IcmpHeader buildEchoRequest(std::uint16_t identifier, std::uint16_t sequence);

    IcmpType type() const;
    void setType(IcmpType type);

    IcmpCode code() const;
    void setCode(IcmpCode code);

    std::uint16_t checksum() const;
    void setChecksum(std::uint16_t checksum);

    std::uint16_t identifier() const;
    void setIdentifier(std::uint16_t identifier);

    std::uint16_t sequence() const;
    void setSequence(std::uint16_t sequence);

private:
    IcmpType m_type{IcmpType::EchoRequest};
    IcmpCode m_code{IcmpCode::Echo};
    std::uint16_t m_checksum{0};
    std::uint16_t m_identifier{0};
    std::uint16_t m_sequence{0};
};

class IcmpPacket final
{
public:
    IcmpPacket();
    explicit IcmpPacket(IcmpHeader header);
    IcmpPacket(IcmpHeader header, std::vector<std::uint8_t> payload);

    const IcmpHeader& header() const;
    IcmpHeader& header();

    IcmpType type() const;
    IcmpCode code() const;
    std::uint16_t checksum() const;
    std::uint16_t identifier() const;
    std::uint16_t sequence() const;

    const std::vector<std::uint8_t>& payload() const;
    std::vector<std::uint8_t>& payload();

    void setPayload(const std::vector<std::uint8_t>& payload);
    void setPayload(std::vector<std::uint8_t>&& payload);
    void setPayload(const void* data, std::size_t len);

    std::size_t payloadLen() const;
    bool empty() const;

    std::string dump() const;

private:
    IcmpHeader m_header;
    std::vector<std::uint8_t> m_payload;
};

}
