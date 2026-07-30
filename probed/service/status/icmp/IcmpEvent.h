#pragma once

#include "event/ProbedEvent.h"
#include "icmp/IcmpPacket.h"

#include <cstdint>
#include <memory>
#include <string>

namespace pz::probed
{

class ProbedServiceManager;

enum class IcmpEventType : std::uint32_t
{
    Unknown = 0,
    StartProbe = 1,
    SendProbeBatch = 2,
    EchoReply = 3,
    ProbeCompleted = 4,
};

class IcmpEvent final : public ProbedEvent
{
public:
    explicit IcmpEvent(IcmpEventType type);

    IcmpEvent(IcmpEventType type, std::string srcIp, std::unique_ptr<IcmpPacket> packet);

    IcmpEventType type() const;
    const std::string& srcIp() const;
    const IcmpPacket* packet() const;

    void dispatch(ProbedServiceManager& serviceManager) override;

private:
    IcmpEventType m_type = IcmpEventType::Unknown;
    std::string m_srcIp;
    std::unique_ptr<IcmpPacket> m_packet;
};

}
