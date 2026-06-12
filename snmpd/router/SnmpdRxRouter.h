#pragma once

#include "router/RxRouter.h"
#include "event/SnmpdEvent.h"
#include "event/SnmpdEventFactory.h"
#include "service/SnmpdServiceManager.h"

#include <memory>

namespace pz::snmpd
{

class SnmpdPacket;

class SnmpdRxRouter : public pz::router::RxRouter
{
public:
    SnmpdRxRouter(SnmpdEventFactory* eventFactory);
    ~SnmpdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // Called by SnmpEngineHandler when an async scan sweep completes. Mirrors
    // IcmpdRxRouter::handleIcmpPacket — the typed packet is turned into an event
    // by the factory, never constructed inline here.
    void handleSnmpPacket(std::unique_ptr<SnmpdPacket> packet);

    void setServiceManager(SnmpdServiceManager* serviceManager);

private:
    SnmpdServiceManager* m_serviceManager = nullptr;
    SnmpdEventFactory* m_eventFactory = nullptr;
};

} // namespace pz::snmpd
