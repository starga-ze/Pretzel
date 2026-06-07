#pragma once

#include "router/RxRouter.h"
#include "event/SnmpdEvent.h"
#include "event/SnmpdEventFactory.h"
#include "service/SnmpdServiceManager.h"

namespace pz::snmpd
{

class SnmpdRxRouter : public pz::router::RxRouter
{
public:
    SnmpdRxRouter(SnmpdEventFactory* eventFactory);
    ~SnmpdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void setServiceManager(SnmpdServiceManager* serviceManager);

private:
    SnmpdServiceManager* m_serviceManager = nullptr;
    SnmpdEventFactory* m_eventFactory = nullptr;
};

} // namespace pz::snmpd
