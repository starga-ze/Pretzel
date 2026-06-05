#include "ipc/IpcClientHandler.h"
#include "router/EnginedTxRouter.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedTxRouter::EnginedTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler) : 
    m_ipcClientHandler(ipcClientHandler)
{
}

void EnginedTxRouter::handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
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

void EnginedTxRouter::sendClientHello()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined, 
            nf::ipc::IpcDaemon::Ipcd, 
            nf::ipc::IpcCmd::ClientHello,
            0, 
            flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    handleIpcMessage(std::move(msg));
}

void EnginedTxRouter::sendSyncRequest()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    auto flag = nf::ipc::IpcProtocol::toFlag(nf::ipc::IpcFlag::Request);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined, 
            nf::ipc::IpcDaemon::Ipcd, 
            nf::ipc::IpcCmd::SyncRequest,
            0, 
            flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    handleIpcMessage(std::move(msg));
}

void EnginedTxRouter::sendRuntimeStart()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    auto flag = nf::ipc::IpcProtocol::orFlag(nf::ipc::IpcFlag::Request, nf::ipc::IpcFlag::Broadcast);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined, 
            nf::ipc::IpcDaemon::Broadcast,
            nf::ipc::IpcCmd::RuntimeStart, 
            0,
            flag);

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());

    handleIpcMessage(std::move(msg));
}

} // namespace nf::engined
