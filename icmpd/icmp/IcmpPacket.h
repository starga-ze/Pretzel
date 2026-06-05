#pragma once

#include "icmp/IcmpProtocol.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nf::icmpd
{

class IcmpPacket final
{
public:
    IcmpPacket();

    IcmpPacket(IcmpType type,
               IcmpCode code,
               std::uint16_t identifier,
               std::uint16_t sequence,
               std::vector<std::uint8_t> payload = {});

    static IcmpPacket buildEchoRequest(std::uint16_t identifier,
                                       std::uint16_t sequence,
                                       std::vector<std::uint8_t> payload = {});

    IcmpType type() const;
    void setType(IcmpType type);

    IcmpCode code() const;
    void setCode(IcmpCode code);

    std::uint16_t checksum() const;

    std::uint16_t identifier() const;
    void setIdentifier(std::uint16_t identifier);

    std::uint16_t sequence() const;
    void setSequence(std::uint16_t sequence);

    const std::vector<std::uint8_t>& payload() const;
    std::vector<std::uint8_t>& payload();

    void setPayload(const std::vector<std::uint8_t>& payload);
    void setPayload(std::vector<std::uint8_t>&& payload);
    void setPayload(const void* data, std::size_t len);

    std::size_t payloadLen() const;
    std::size_t wireLen() const;
    bool empty() const;

    std::vector<std::uint8_t> toWire() const;
    std::vector<std::uint8_t> serialize() const;

    static std::unique_ptr<IcmpPacket> fromWire(const std::vector<std::uint8_t>& wire);
    static std::unique_ptr<IcmpPacket> parse(const std::vector<std::uint8_t>& wire);

    std::string dump() const;

private:
    static std::uint16_t calculateChecksum(const std::uint8_t* data,
                                           std::size_t len);

private:
    IcmpType m_type {IcmpType::EchoRequest};
    IcmpCode m_code {IcmpCode::Echo};
    std::uint16_t m_identifier {0};
    std::uint16_t m_sequence {0};
    std::vector<std::uint8_t> m_payload;
};

} // namespace nf::icmpd
