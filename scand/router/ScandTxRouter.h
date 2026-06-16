#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "snmp/SnmpEngine.h"
#include "snmp/SnmpTypes.h"

#include <string>
#include <vector>

namespace pz::scand
{

class ScandTxRouter : public pz::router::TxRouter
{
public:
    ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler,
                  SnmpEngine* snmpEngine);
    ~ScandTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // Kick off an async SNMP sweep via SnmpEngine directly.
    void startScan(std::vector<std::string> ips, SnmpScanConfig cfg);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
    SnmpEngine*                m_snmpEngine;
};

} // namespace pz::scand
