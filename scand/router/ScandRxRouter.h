#pragma once

#include "router/RxRouter.h"
#include "event/ScandEvent.h"
#include "event/ScandEventFactory.h"
#include "service/ScandServiceManager.h"

#include <memory>

namespace pz::scand
{

class ScandPacket;

class ScandRxRouter : public pz::router::RxRouter
{
public:
    ScandRxRouter(ScandEventFactory* eventFactory);
    ~ScandRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    // Called by SnmpEngineHandler when an async scan sweep completes. Mirrors
    // IcmpdRxRouter::handleIcmpPacket — the typed packet is turned into an event
    // by the factory, never constructed inline here.
    void handleSnmpPacket(std::unique_ptr<ScandPacket> packet);

    void setServiceManager(ScandServiceManager* serviceManager);

private:
    ScandServiceManager* m_serviceManager = nullptr;
    ScandEventFactory* m_eventFactory = nullptr;
};

} // namespace pz::scand
