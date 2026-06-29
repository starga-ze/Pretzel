#pragma once

#include "router/TxRouter.h"
#include "ipc/IpcClientHandler.h"
#include "snmp/SnmpEngineHandler.h"
#include "snmp/SnmpTypes.h"
#include "api/ApiEngineHandler.h"
#include "api/ApiTypes.h"

#include <map>
#include <string>
#include <vector>

namespace pz::scand
{

class ScandTxRouter : public pz::router::TxRouter
{
public:
    ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler,
                  SnmpEngineHandler* snmpEngineHandler,
                  ApiEngineHandler* apiEngineHandler);
    ~ScandTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // Kick off an async v2c/v3 sweep via SnmpEngineHandler::egress (mirrors
    // IcmpdTxRouter::handleIcmpPacket delegating to IcmpEngineHandler::egress).
    void handleSnmpPacket(std::vector<std::string> ips, SnmpScanConfig cfg);

    // Kick off an async vendor-API sweep via ApiEngineHandler::egress — symmetric
    // counterpart to handleSnmpPacket above, for the "api" scan method.
    void handleApiPacket(std::map<std::string, ApiCredential> devices);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
    SnmpEngineHandler*         m_snmpEngineHandler;
    ApiEngineHandler*          m_apiEngineHandler;
};

} // namespace pz::scand
