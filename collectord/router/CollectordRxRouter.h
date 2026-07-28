#pragma once

#include "event/CollectordEvent.h"
#include "event/CollectordEventFactory.h"
#include "router/RxRouter.h"
#include "service/CollectordServiceManager.h"

#include <memory>

namespace pz::collectord
{

class CollectordRxRouter : public pz::router::RxRouter
{
public:
    CollectordRxRouter(CollectordEventFactory* eventFactory);
    ~CollectordRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void setServiceManager(CollectordServiceManager* serviceManager);

private:
    CollectordServiceManager* m_serviceManager = nullptr;
    CollectordEventFactory* m_eventFactory = nullptr;
};

}
