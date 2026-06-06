#pragma once

#include "router/RxRouter.h"
#include "event/TopologydEventFactory.h"
#include "ipc/IpcMessage.h"

#include <memory>

namespace nf::topologyd
{

class TopologydServiceManager;

class TopologydRxRouter : public nf::router::RxRouter
{
public:
    explicit TopologydRxRouter(TopologydEventFactory* eventFactory);

    void handleIpcMessage(std::unique_ptr<nf::ipc::IpcMessage> msg) override;

    void setServiceManager(TopologydServiceManager* serviceManager);

private:
    TopologydEventFactory*    m_eventFactory{nullptr};
    TopologydServiceManager*  m_serviceManager{nullptr};
};

} // namespace nf::topologyd
