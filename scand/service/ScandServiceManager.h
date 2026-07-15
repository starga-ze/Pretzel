#pragma once

#include "service/ServiceManager.h"

#include "service/api/ApiService.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/reload/ReloadService.h"
#include "service/scan/ScanService.h"

#include "router/ScandTxRouter.h"

#include <queue>

namespace pz::scand
{

class ScandEventFactory;
class ScandActionFactory;

class ScandServiceManager : public pz::service::ServiceManager<ScandEvent, ScandAction>
{
public:
    ScandServiceManager(ScandEventFactory* eventFactory, ScandActionFactory* actionFactory, ScandTxRouter* txRouter);
    ~ScandServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<ScandEvent> event) override;
    void postAction(std::unique_ptr<ScandAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();
    ScanService& scanService();
    ReloadService& reloadService();
    ApiService& apiService();

    ScandTxRouter& txRouter();

private:
    ScandEventFactory* m_eventFactory;
    ScandActionFactory* m_actionFactory;
    ScandTxRouter* m_txRouter;

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ScanService> m_scanService;
    std::unique_ptr<ReloadService> m_reloadService;
    std::unique_ptr<ApiService> m_apiService;

    std::queue<std::unique_ptr<ScandEvent>> m_eventQueue;
    std::queue<std::unique_ptr<ScandAction>> m_actionQueue;
};

}
