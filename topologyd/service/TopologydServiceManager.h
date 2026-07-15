#pragma once

#include "service/ServiceManager.h"

#include "action/TopologydAction.h"
#include "event/TopologydEvent.h"

#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/reload/ReloadService.h"

#include "router/TopologydTxRouter.h"

#include <memory>
#include <queue>

namespace pz::topologyd
{

class TopologydEventFactory;
class TopologydActionFactory;

class TopologydServiceManager : public pz::service::ServiceManager<TopologydEvent, TopologydAction>
{
public:
    TopologydServiceManager(TopologydEventFactory* eventFactory, TopologydActionFactory* actionFactory,
                            TopologydTxRouter* txRouter);
    ~TopologydServiceManager() override = default;

    void start() override;
    void schedule() override;
    void postEvent(std::unique_ptr<TopologydEvent> event) override;
    void postAction(std::unique_ptr<TopologydAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();
    ReloadService& reloadService();

    TopologydTxRouter& txRouter();

private:
    TopologydEventFactory* m_eventFactory{nullptr};
    TopologydActionFactory* m_actionFactory{nullptr};
    TopologydTxRouter* m_txRouter{nullptr};

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ReloadService> m_reloadService;

    std::queue<std::unique_ptr<TopologydEvent>> m_eventQueue;
    std::queue<std::unique_ptr<TopologydAction>> m_actionQueue;
};

}
