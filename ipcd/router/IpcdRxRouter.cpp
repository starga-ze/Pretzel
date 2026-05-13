#include "router/IpcdRxRouter.h"
#include "util/Logger.h"

namespace nf::ipcd
{

void IpcdRxRouter::handleMessage(std::unique_ptr<nf::ipc::IpcMessage> msg)
{
    LOG_DEBUG("Ipcd Rx Router handle Message");
}

}

