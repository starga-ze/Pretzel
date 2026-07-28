#include "router/ProbedTxRouter.h"
#include "util/Logger.h"

namespace pz::probed
{

ProbedTxRouter::ProbedTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, IcmpEngineHandler* icmpEngineHandler)
    : m_ipcClientHandler(ipcClientHandler), m_icmpEngineHandler(icmpEngineHandler)
{
}

void ProbedTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("Message is not initialized");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void ProbedTxRouter::handleIcmpPacket(std::unique_ptr<IcmpPacket> packet, std::string dstIp)
{
    m_icmpEngineHandler->egress(std::move(packet), std::move(dstIp));
}

}
