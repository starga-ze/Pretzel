#pragma once

#include "api/ApiEngineHandler.h"
#include "api/ApiTypes.h"
#include "ipc/IpcClientHandler.h"
#include "router/TxRouter.h"
#include "snmp/SnmpEngineHandler.h"
#include "snmp/SnmpTypes.h"

#include <map>
#include <string>
#include <vector>

namespace pz::scand
{

class ScandTxRouter : public pz::router::TxRouter
{
public:
    ScandTxRouter(pz::ipc::IpcClientHandler* ipcClientHandler, SnmpEngineHandler* snmpEngineHandler,
                  ApiEngineHandler* apiEngineHandler);
    ~ScandTxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void handleSnmpPacket(std::vector<std::string> ips, SnmpScanConfig cfg);

    void handleApiPacket(std::map<std::string, ApiCredential> devices);

private:
    pz::ipc::IpcClientHandler* m_ipcClientHandler;
    SnmpEngineHandler* m_snmpEngineHandler;
    ApiEngineHandler* m_apiEngineHandler;
};

}
