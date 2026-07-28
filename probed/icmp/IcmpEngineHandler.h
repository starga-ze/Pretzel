#pragma once

#include "icmp/IcmpCodec.h"
#include "icmp/IcmpConnection.h"
#include "icmp/IcmpPacket.h"
#include "io/Epoll.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pz::probed
{

class IcmpEngine;
class ProbedRxRouter;

class IcmpEngineHandler final
{
public:
    explicit IcmpEngineHandler(IcmpEngine* icmpEngine);

    bool handleRecv(int fd, IcmpConnection& conn, pz::io::Epoll& epoll);
    bool handleSend(int fd, IcmpConnection& conn, pz::io::Epoll& epoll);

    bool ingress(int fd, const std::string& srcIp, IcmpFrameView frame);

    void egress(std::unique_ptr<IcmpPacket> packet, std::string dstIp);

    void setRxRouter(ProbedRxRouter* rxRouter);

private:
    IcmpEngine* m_icmpEngine{nullptr};
    ProbedRxRouter* m_rxRouter{nullptr};

    IcmpCodec m_codec;
};

}
