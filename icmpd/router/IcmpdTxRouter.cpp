#include "ipc/IpcClientHandler.h"
#include "router/IcmpdTxRouter.h"
#include "util/Logger.h"

namespace nf::icmpd
{

IcmpdTxRouter::IcmpdTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler) : 
    m_ipcClientHandler(ipcClientHandler)
{
}

void IcmpdTxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Icmpd Tx Router handle Message");

    if (!msg)
    {
        LOG_WARN("Message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_FATAL("IpcClientHandler is nullptr");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void IcmpdTxRouter::sendClientHello()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Icmpd);

    auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Icmpd, 
            nf::ipc::IpcDaemon::Ipcd, 
            nf::ipc::IpcCmd::ClientHello,
            0, 
            flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    handleMessage(std::move(msg));
}

void IcmpdTxRouter::sendRuntimeReady()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Icmpd);

    auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Icmpd, 
            nf::ipc::IpcDaemon::Ipcd, 
            nf::ipc::IpcCmd::RuntimeReady,
            0, 
            flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    handleMessage(std::move(msg));
}

} // namespace nf::icmpd
