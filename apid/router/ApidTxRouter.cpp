#include "router/ApidTxRouter.h"

#include "util/Logger.h"

namespace pz::apid
{

ApidTxRouter::ApidTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, pz::http::HttpHandler* httpHandler)
    : m_ipcClientHandler(ipcClientHandler), m_httpHandler(httpHandler)
{
}

void ApidTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_WARN("message is not initialized");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IPC client handler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void ApidTxRouter::handleHttpMessage(pz::http::HttpResponse response, pz::http::SessionId id)
{
    if (!m_httpHandler)
    {
        LOG_ERROR("HTTP handler is not initialized");
        return;
    }

    m_httpHandler->egress(std::move(response), id);
}

}
