#pragma once

#include "event/IcmpdEvent.h"
#include "icmp/IcmpPacket.h"

#include <cstdint>
#include <memory>
#include <string>

namespace nf::icmpd
{

class IcmpdServiceManager;

enum class ProbeEventType : std::uint32_t
{
    Unknown            = 0,
    StartProbe         = 1,
    SendProbeBatch     = 2,
    EchoReply          = 3,
    ProbeCompleted     = 4,
};

class ProbeEvent final : public IcmpdEvent
{
public:
    explicit ProbeEvent(ProbeEventType type);

    ProbeEvent(ProbeEventType type,
               std::string srcIp,
               std::unique_ptr<IcmpPacket> packet);

    ProbeEventType type() const;
    const std::string& srcIp() const;
    const IcmpPacket* packet() const;

    void dispatch(IcmpdServiceManager& serviceManager) override;

private:
    ProbeEventType m_type = ProbeEventType::Unknown;
    std::string m_srcIp;
    std::unique_ptr<IcmpPacket> m_packet;
};

} // namespace nf::icmpd
