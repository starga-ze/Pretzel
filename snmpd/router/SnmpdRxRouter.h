#pragma once

#include "router/RxRouter.h"
#include "event/SnmpdEvent.h"
#include "event/SnmpdEventFactory.h"
#include "service/SnmpdServiceManager.h"
#include "snmp/SnmpTypes.h"

#include <vector>

namespace pz::snmpd
{

class SnmpdRxRouter : public pz::router::RxRouter
{
public:
    SnmpdRxRouter(SnmpdEventFactory* eventFactory);
    ~SnmpdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // Called by SnmpEngineHandler when async scan sweep completes.
    void handleSnmpScanComplete(std::vector<SnmpDevice> devices);

    void setServiceManager(SnmpdServiceManager* serviceManager);

private:
    SnmpdServiceManager* m_serviceManager = nullptr;
    SnmpdEventFactory* m_eventFactory = nullptr;
};

} // namespace pz::snmpd
