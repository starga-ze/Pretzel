#include "router/IpcdRxRouter.h"
#include "ipc/IpcServerHandler.h"
#include "util/Logger.h"

namespace nf::ipcd
{

IpcdRxRouter::IpcdRxRouter(IpcServerHandler* ipcServerHandler, IpcdTxRouter* txRouter) : 
    m_ipcServerHandler(ipcServerHandler),
    m_txRouter(txRouter)
{
}

void IpcdRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    if (!m_txRouter or !m_ipcServerHandler)
    {
        LOG_ERROR("Dependency is not ready, (txRouter={}, ipcServerHandler={})",
                static_cast<bool>(m_txRouter),
                static_cast<bool>(m_ipcServerHandler));
        return;
    }

    if (msg->getCmd() == nf::ipc::IpcCmd::RuntimeReady)
    {
        m_ipcServerHandler->markRuntimeReady(msg->getSrc(), true);
        return;
    }

    m_txRouter->handleMessage(std::move(msg));
}

void IpcdRxRouter::handleEvent(std::unique_ptr<nf::event::Event> event)
{

}

void IpcdRxRouter::setProcess(IpcdProcess* process)
{
    m_process = process;
}

} // namespace nf::ipcd
