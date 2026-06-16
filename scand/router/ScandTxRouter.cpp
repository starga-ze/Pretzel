#include "router/ScandTxRouter.h"
#include "util/Logger.h"

namespace pz::scand
{

ScandTxRouter::ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler,
                               SnmpEngine* snmpEngine)
    : m_ipcClientHandler(ipcClientHandler),
      m_snmpEngine(snmpEngine)
{
}

void ScandTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_ERROR("ScandTxRouter: message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("ScandTxRouter: IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void ScandTxRouter::startScan(std::vector<std::string> ips, SnmpScanConfig cfg)
{
    if (!m_snmpEngine)
    {
        LOG_ERROR("ScandTxRouter: SnmpEngine is not initialized");
        return;
    }

    m_snmpEngine->startScan(std::move(ips), std::move(cfg));
}

} // namespace pz::scand
