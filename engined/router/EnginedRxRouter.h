#pragma once

#include "event/EnginedEvent.h"
#include "event/EnginedEventFactory.h"
#include "router/RxRouter.h"
#include "service/EnginedServiceManager.h"

namespace pz::engined
{

class EnginedRxRouter : public pz::router::RxRouter
{
public:
    EnginedRxRouter(EnginedEventFactory* eventFactory, EnginedServiceManager* serviceManager);
    ~EnginedRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedServiceManager* m_serviceManager{nullptr};
};

}
