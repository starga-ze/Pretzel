#include "router/ScandTxRouter.h"
#include "util/Logger.h"

namespace pz::scand
{

ScandTxRouter::ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, SnmpEngineHandler* snmpEngineHandler,
                             ApiEngineHandler* apiEngineHandler)
    : m_ipcClientHandler(ipcClientHandler), m_snmpEngineHandler(snmpEngineHandler), m_apiEngineHandler(apiEngineHandler)
{
}

void ScandTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_ERROR("message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void ScandTxRouter::handleSnmpPacket(std::vector<std::string> ips, SnmpScanConfig cfg)
{
    if (!m_snmpEngineHandler)
    {
        LOG_ERROR("SnmpEngineHandler is not initialized");
        return;
    }

    m_snmpEngineHandler->egress(std::move(ips), std::move(cfg));
}

void ScandTxRouter::handleApiPacket(std::map<std::string, ApiCredential> devices)
{
    if (!m_apiEngineHandler)
    {
        LOG_ERROR("ApiEngineHandler is not initialized");
        return;
    }

    m_apiEngineHandler->egress(std::move(devices));
}

}
