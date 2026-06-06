#pragma once

#include "router/RxRouter.h"
#include "event/SnmpdEvent.h"
#include "event/SnmpdEventFactory.h"
#include "service/SnmpdServiceManager.h"

namespace nf::snmpd
{

class SnmpdRxRouter : public nf::router::RxRouter
{
public:
    SnmpdRxRouter(SnmpdEventFactory* eventFactory);
    ~SnmpdRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setServiceManager(SnmpdServiceManager* serviceManager);

private:
    SnmpdServiceManager* m_serviceManager = nullptr;
    SnmpdEventFactory* m_eventFactory = nullptr;
};

} // namespace nf::snmpd
