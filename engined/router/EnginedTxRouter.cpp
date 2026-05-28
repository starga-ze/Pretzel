#include "ipc/IpcClientHandler.h"
#include "router/EnginedTxRouter.h"
#include "util/Logger.h"

namespace nf::engined
{

EnginedTxRouter::EnginedTxRouter(nf::ipc::IpcClientHandler* ipcClientHandler) : 
    m_ipcClientHandler(ipcClientHandler)
{
}

void EnginedTxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Engined Tx Router handle Message");

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

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined,
            nf::ipc::IpcDaemon::Ipcd,
            nf::ipc::IpcCmd::ClientHello,
            0,
            static_cast<std::uint8_t>(nf::ipc::IpcFlag::Request)
            );

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()),
                name.size());

    handleMessage(std::move(msg));
}

void EnginedTxRouter::sendRuntimeRequest()
{
    std::string name = nf::ipc::IpcProtocol::daemonToStr(nf::ipc::IpcDaemon::Engined);

    nf::ipc::IpcHeader header = nf::ipc::IpcHeader::build(
            nf::ipc::IpcDaemon::Engined,
            nf::ipc::IpcDaemon::Broadcast,
            nf::ipc::IpcCmd::RuntimeRequest,
            0,
            static_cast<std::uint8_t>(nf::ipc::IpcFlag::Request)
            );

    auto msg = std::make_unique<nf::ipc::IpcMessage>(std::move(header));
    msg->setPayload(reinterpret_cast<const std::uint8_t*>(name.data()),
                name.size());

    handleMessage(std::move(msg));
   
}

} // namespace nf::engined
