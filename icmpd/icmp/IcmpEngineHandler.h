#pragma once

#include "icmp/IcmpCodec.h"
#include "icmp/IcmpConnection.h"
#include "icmp/IcmpPacket.h"
#include "io/Epoll.h"

#include <memory>
#include <queue>
#include <string>

namespace nf::icmpd
{

class IcmpEngine;
class IcmpdRxRouter;

class IcmpEngineHandler final
{
public:
    explicit IcmpEngineHandler(IcmpEngine* icmpEngine);

    bool handleRecv(int fd, IcmpConnection& conn, nf::io::Epoll& epoll);
    bool handleSend(int fd, IcmpConnection& conn, nf::io::Epoll& epoll);

    bool ingress(int fd,
                 const std::string& srcIp,
                 std::vector<std::uint8_t> bytes);

    void egress(std::unique_ptr<IcmpPacket> packet,
                std::string dstIp);

    bool enqueuePacket(std::unique_ptr<IcmpPacket> packet,
                       std::string dstIp);

    void setRxRouter(IcmpdRxRouter* rxRouter);

private:
    struct TxItem
    {
        std::unique_ptr<IcmpPacket> packet;
        std::string dstIp;
    };

private:
    IcmpEngine* m_icmpEngine {nullptr};
    IcmpdRxRouter* m_rxRouter {nullptr};

    IcmpCodec m_codec;
    std::queue<TxItem> m_txQueue;
};

} // namespace nf::icmpd
