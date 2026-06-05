#include "icmp/IcmpEngineHandler.h"

#include "icmp/IcmpEngine.h"
#include "router/IcmpdRxRouter.h"
#include "util/Logger.h"

#include <sys/epoll.h>

namespace nf::icmpd
{

IcmpEngineHandler::IcmpEngineHandler(IcmpEngine* icmpEngine)
    : m_icmpEngine(icmpEngine)
{
}

bool IcmpEngineHandler::handleRecv(int fd,
                                   IcmpConnection& conn,
                                   nf::io::Epoll& epoll)
{
    (void)epoll;

    while (true)
    {
        std::vector<std::uint8_t> bytes;
        std::string srcIp;
        int outErrno = 0;

        const auto rc = conn.recv(bytes, srcIp, outErrno);
        switch (rc)
        {
        case IcmpIoResult::Ok:
            if (!ingress(fd, srcIp, std::move(bytes)))
                return false;
            continue;

        case IcmpIoResult::WouldBlock:
            return true;

        case IcmpIoResult::PeerClosed:
            LOG_WARN("IcmpEngineHandler: recv peer closed fd={}", fd);
            return false;

        case IcmpIoResult::Error:
            LOG_WARN("IcmpEngineHandler: recv failed fd={} errno={}", fd, outErrno);
            return false;
        }
    }
}

bool IcmpEngineHandler::handleSend(int fd,
                                   IcmpConnection& conn,
                                   nf::io::Epoll& epoll)
{
    while (!m_txQueue.empty())
    {
        TxItem& item = m_txQueue.front();

        if (!item.packet)
        {
            LOG_WARN("IcmpEngineHandler: tx packet is nullptr fd={}", fd);
            m_txQueue.pop();
            continue;
        }

        LOG_TRACE("ICMP Egress Packet dump dst={}:\n{}", item.dstIp, item.packet->dump());

        const auto bytes = m_codec.encode(*item.packet);
        if (bytes.empty())
        {
            LOG_WARN("IcmpEngineHandler: encode failed fd={} dst={}", fd, item.dstIp);
            m_txQueue.pop();
            continue;
        }

        int outErrno = 0;
        const auto rc = conn.send(bytes, item.dstIp, outErrno);

        switch (rc)
        {
        case IcmpIoResult::Ok:
            m_txQueue.pop();
            continue;

        case IcmpIoResult::WouldBlock:
            return true;

        case IcmpIoResult::PeerClosed:
            LOG_WARN("IcmpEngineHandler: send peer closed fd={} dst={}", fd, item.dstIp);
            return false;

        case IcmpIoResult::Error:
            LOG_WARN("IcmpEngineHandler: send failed fd={} dst={} errno={}",
                     fd,
                     item.dstIp,
                     outErrno);
            return false;
        }
    }

    if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
    {
        LOG_ERROR("IcmpEngineHandler: epoll mod remove EPOLLOUT failed fd={}", fd);
        return false;
    }

    return true;
}

bool IcmpEngineHandler::ingress(int fd,
                                const std::string& srcIp,
                                std::vector<std::uint8_t> bytes)
{
    if (bytes.empty())
    {
        LOG_WARN("IcmpEngineHandler: ingress packet is empty fd={} src={}", fd, srcIp);
        return false;
    }

    std::unique_ptr<IcmpPacket> packet;
    const auto rc = m_codec.decode(bytes, packet);
    if (rc != IcmpDecodeResult::Ok)
    {
        LOG_WARN("IcmpEngineHandler: ingress decode failed fd={} src={} rc={} size={}",
                 fd,
                 srcIp,
                 static_cast<int>(rc),
                 bytes.size());
        return true;
    }

    if (!packet)
    {
        LOG_WARN("IcmpEngineHandler: ingress decode returned nullptr fd={} src={}", fd, srcIp);
        return true;
    }

    LOG_TRACE("ICMP Ingress Packet dump src={}:\n{}", srcIp, packet->dump());

    if (!m_rxRouter)
    {
        LOG_WARN("IcmpEngineHandler: rxRouter is nullptr");
        return false;
    }

    m_rxRouter->handleIcmpPacket(srcIp, std::move(packet));
    return true;
}

void IcmpEngineHandler::egress(std::unique_ptr<IcmpPacket> packet,
                               std::string dstIp)
{
    if (!packet)
    {
        LOG_WARN("IcmpEngineHandler: egress packet is nullptr dst={}", dstIp);
        return;
    }

    if (!m_icmpEngine)
    {
        LOG_FATAL("IcmpEngine is nullptr");
        return;
    }

    LOG_TRACE("ICMP Egress Request dump dst={}:\n{}", dstIp, packet->dump());

    if (!m_icmpEngine->enqueuePacket(std::move(packet), std::move(dstIp)))
        LOG_WARN("IcmpEngineHandler: egress enqueue failed");
}

bool IcmpEngineHandler::enqueuePacket(std::unique_ptr<IcmpPacket> packet,
                                      std::string dstIp)
{
    if (!packet)
    {
        LOG_WARN("IcmpEngineHandler: enqueue packet is nullptr dst={}", dstIp);
        return false;
    }

    if (dstIp.empty())
    {
        LOG_WARN("IcmpEngineHandler: enqueue dstIp is empty");
        return false;
    }

    m_txQueue.push(TxItem {
        std::move(packet),
        std::move(dstIp),
    });

    return true;
}

void IcmpEngineHandler::setRxRouter(IcmpdRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace nf::icmpd
