#include "icmp/IcmpEngineHandler.h"

#include "icmp/IcmpEngine.h"
#include "router/IcmpdRxRouter.h"
#include "util/Logger.h"

#include <sys/epoll.h>

namespace pz::icmpd
{

IcmpEngineHandler::IcmpEngineHandler(IcmpEngine* icmpEngine)
    : m_icmpEngine(icmpEngine)
{
}

bool IcmpEngineHandler::handleRecv(int fd,
                                   IcmpConnection& conn,
                                   pz::io::Epoll& epoll)
{
    (void)epoll;

    int outErrno = 0;
    const auto rc = conn.recv(outErrno);

    switch (rc)
    {
    case IcmpIoResult::Ok:
    case IcmpIoResult::WouldBlock:
        break;

    case IcmpIoResult::BufferFull:
        LOG_WARN("IcmpEngineHandler: rx queue full fd={} queued={}",
                 fd,
                 conn.rxQueueSize());
        break;

    case IcmpIoResult::PeerClosed:
        LOG_WARN("IcmpEngineHandler: recv peer closed fd={}", fd);
        return false;

    case IcmpIoResult::Error:
        LOG_WARN("IcmpEngineHandler: recv failed fd={} errno={}", fd, outErrno);
        return false;
    }

    IcmpConnection::RxFrame rxFrame;
    while (conn.read(rxFrame))
    {
        IcmpFrameView frame {
            rxFrame.bytes.data(),
            rxFrame.bytes.size(),
        };

        if (!ingress(fd, rxFrame.srcIp, frame))
            return false;
    }

    return true;
}

bool IcmpEngineHandler::handleSend(int fd,
                                   IcmpConnection& conn,
                                   pz::io::Epoll& epoll)
{
    int outErrno = 0;
    const auto rc = conn.send(outErrno);

    switch (rc)
    {
    case IcmpIoResult::Ok:
        break;

    case IcmpIoResult::WouldBlock:
        return true;

    case IcmpIoResult::BufferFull:
        LOG_WARN("IcmpEngineHandler: unexpected tx buffer full fd={}", fd);
        return true;

    case IcmpIoResult::PeerClosed:
        LOG_WARN("IcmpEngineHandler: send peer closed fd={}", fd);
        return false;

    case IcmpIoResult::Error:
        LOG_WARN("IcmpEngineHandler: send failed fd={} errno={}", fd, outErrno);
        return false;
    }

    if (!conn.hasPendingTx())
    {
        if (!epoll.mod(fd, EPOLLIN | EPOLLRDHUP))
        {
            LOG_ERROR("IcmpEngineHandler: epoll mod remove EPOLLOUT failed fd={}", fd);
            return false;
        }
    }

    return true;
}

bool IcmpEngineHandler::ingress(int fd,
                                const std::string& srcIp,
                                IcmpFrameView frame)
{
    if (frame.empty())
    {
        LOG_WARN("IcmpEngineHandler: ingress packet is empty fd={} src={}", fd, srcIp);
        return false;
    }

    std::unique_ptr<IcmpPacket> packet;
    const auto rc = m_codec.decode(frame, packet);
    if (rc != IcmpDecodeResult::Ok)
    {
        LOG_WARN("IcmpEngineHandler: ingress decode failed fd={} src={} rc={} size={}",
                 fd,
                 srcIp,
                 static_cast<int>(rc),
                 frame.size);
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
        LOG_WARN("IcmpEngineHandler: rxRouter is nullptr, drop packet src={}", srcIp);
        return true;
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

    if (dstIp.empty())
    {
        LOG_WARN("IcmpEngineHandler: egress dstIp is empty");
        return;
    }

    if (!m_icmpEngine)
    {
        LOG_ERROR("IcmpEngine is nullptr");
        return;
    }

    LOG_TRACE("ICMP Egress Request dump dst={}:\n{}", dstIp, packet->dump());

    std::vector<std::uint8_t> frame = m_codec.encode(packet);
    if (frame.empty())
    {
        LOG_WARN("IcmpEngineHandler: encode failed dst={}", dstIp);
        return;
    }

    if (!m_icmpEngine->enqueueFrame(std::move(frame), std::move(dstIp)))
        LOG_WARN("IcmpEngineHandler: egress enqueue failed");
}

void IcmpEngineHandler::setRxRouter(IcmpdRxRouter* rxRouter)
{
    m_rxRouter = rxRouter;
}

} // namespace pz::icmpd
