#pragma once

#include "event/ScandEvent.h"
#include "event/ScandEventFactory.h"
#include "router/RxRouter.h"
#include "service/ScandServiceManager.h"

#include <memory>

namespace pz::scand
{

class ScandRxRouter : public pz::router::RxRouter
{
public:
    ScandRxRouter(ScandEventFactory* eventFactory);
    ~ScandRxRouter() override = default;

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void setServiceManager(ScandServiceManager* serviceManager);

private:
    ScandServiceManager* m_serviceManager = nullptr;
    ScandEventFactory* m_eventFactory = nullptr;
};

}
