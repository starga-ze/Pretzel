#pragma once

#include "router/RxRouter.h"
#include "event/TopologydEventFactory.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace pz::topologyd
{

class TopologydServiceManager;

class TopologydRxRouter : public pz::router::RxRouter
{
public:
    explicit TopologydRxRouter(TopologydEventFactory* eventFactory);

    void handleIpcMessage(std::unique_ptr<pz::ipc::IpcMessage> msg) override;

    void setServiceManager(TopologydServiceManager* serviceManager);

private:
    TopologydEventFactory*    m_eventFactory{nullptr};
    TopologydServiceManager*  m_serviceManager{nullptr};
};

} // namespace pz::topologyd
