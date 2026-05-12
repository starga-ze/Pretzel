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

    m_ipcServerHandler->onTxMessage(std::move(msg));
}

} // namespace nf::ipcd
