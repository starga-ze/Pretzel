#include "router/IpcdTxRouter.h"
#include "ipc/IpcServerHandler.h"
#include "util/Logger.h"

namespace nf::ipcd
{

IpcdTxRouter::IpcdTxRouter(IpcServerHandler* ipcServerHandler) :
    m_ipcServerHandler(ipcServerHandler)
{

}

void IpcdTxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Ipcd Tx Router handle Message");

    if (!msg)
    {
        LOG_WARN("Message is nullptr");
        return;
    }

    if (!m_ipcServerHandler)
    {
        LOG_ERROR("IpcServerHandler is nullptr");
        return;
    }

    if (msg->getCmd() == nf::ipc::IpcCmd::ClientHello)
    {
        auto res = makeServerHello(*msg);
        m_ipcServerHandler->egress(std::move(res));
        return;
    }

    if (msg->getCmd() == nf::ipc::IpcCmd::SyncRequest)
    {
        auto res = makeSyncResponse(*msg);
        m_ipcServerHandler->egress(std::move(res));
        return;
    }

    LOG_WARN("Unsupported tx message cmd={}", nf::ipc::IpcProtocol::cmdToStr(msg->getCmd()));
}

std::unique_ptr<nf::ipc::IpcMessage>
IpcdTxRouter::makeServerHello(const nf::ipc::IpcMessage& req)
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(
        nf::ipc::IpcDaemon::Ipcd
    );

    auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Response);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
        nf::ipc::IpcDaemon::Ipcd,
        req.getSrc(),
        nf::ipc::IpcCmd::ServerHello,
        req.getSeqNo(),
        flag
    );

    auto response = std::make_unique<nf::ipc::IpcMessage>(std::move(header));

    response->setPayload(
        reinterpret_cast<const std::uint8_t*>(name.data()),
        name.size()
    );

    return response;
}

std::unique_ptr<nf::ipc::IpcMessage>
IpcdTxRouter::makeSyncResponse(const nf::ipc::IpcMessage& req)
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(
            nf::ipc::IpcDaemon::Ipcd
    );

    auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Response);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Ipcd,
            req.getSrc(),
            nf::ipc::IpcCmd::SyncResponse,
            req.getSeqNo(),
            flag
    );

    auto response = std::make_unique<nf::ipc::IpcMessage>(std::move(header));

    response->setPayload(
            reinterpret_cast<const std::uint8_t*>(name.data()),
            name.size()
    );

    return response;
}

} // namespace nf::ipcd
