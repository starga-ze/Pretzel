#include "router/SnmpdTxRouter.h"
#include "util/Logger.h"

namespace pz::snmpd
{

SnmpdTxRouter::SnmpdTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler,
                               SnmpEngine* snmpEngine)
    : m_ipcClientHandler(ipcClientHandler),
      m_snmpEngine(snmpEngine)
{
}

void SnmpdTxRouter::handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg)
{
    if (!msg)
    {
        LOG_ERROR("SnmpdTxRouter: message is nullptr");
        return;
    }

    if (!m_ipcClientHandler)
    {
        LOG_ERROR("SnmpdTxRouter: IpcClientHandler is not initialized");
        return;
    }

    m_ipcClientHandler->egress(std::move(msg));
}

void SnmpdTxRouter::startSnmpScan(std::vector<std::string> ips, SnmpScanConfig cfg)
{
    if (!m_snmpEngine)
    {
        LOG_ERROR("SnmpdTxRouter: SnmpEngine is not initialized");
        return;
    }

    m_snmpEngine->startScan(std::move(ips), std::move(cfg));
}

} // namespace pz::snmpd
