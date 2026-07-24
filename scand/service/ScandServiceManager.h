#pragma once

#include "service/ServiceManager.h"

#include "service/api/ApiCollector.h"
#include "service/api/ApiService.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/reload/ReloadService.h"

#include "router/ScandTxRouter.h"

#include <boost/asio/io_context.hpp>

#include <queue>

namespace pz::scand
{

class ScandEventFactory;
class ScandActionFactory;

class ScandServiceManager : public pz::service::ServiceManager<ScandEvent, ScandAction>
{
public:
    ScandServiceManager(ScandEventFactory* eventFactory, ScandActionFactory* actionFactory, ScandTxRouter* txRouter,
                        boost::asio::io_context* ioContext);
    ~ScandServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<ScandEvent> event) override;
    void postAction(std::unique_ptr<ScandAction> action) override;
    void execute() override;

    ApiService& apiService();
    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();
    ReloadService& reloadService();

    ScandTxRouter& txRouter();

    // Pumped by ScandProcess::tick(), so anything scheduled on it completes on the main loop.
    boost::asio::io_context& ioContext();

private:
    ScandEventFactory* m_eventFactory;
    ScandActionFactory* m_actionFactory;
    ScandTxRouter* m_txRouter;
    boost::asio::io_context* m_ioContext;

    std::unique_ptr<ApiService> m_apiService;
    std::unique_ptr<ApiCollector> m_apiCollector;
    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ReloadService> m_reloadService;

    // One-shot: the issued keys are fetched after bootstrap completes, not at construction.
    bool m_keysRequested{false};

    std::queue<std::unique_ptr<ScandEvent>> m_eventQueue;
    std::queue<std::unique_ptr<ScandAction>> m_actionQueue;
};

}
