#pragma once

#include "service/ServiceManager.h"

#include "service/api/controller/ConnectorController.h"
#include "service/api/ApiService.h"
#include "service/api/controller/StatusController.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/reload/ReloadService.h"

#include "router/CollectordTxRouter.h"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <queue>
#include <string>
#include <unordered_map>

namespace pz::collectord
{

class CollectordEventFactory;
class CollectordActionFactory;

class CollectordServiceManager : public pz::service::ServiceManager<CollectordEvent, CollectordAction>
{
public:
    CollectordServiceManager(CollectordEventFactory* eventFactory, CollectordActionFactory* actionFactory, CollectordTxRouter* txRouter,
                        boost::asio::io_context* ioContext);
    ~CollectordServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<CollectordEvent> event) override;
    void postAction(std::unique_ptr<CollectordAction> action) override;
    void execute() override;

    ApiService& apiService();
    ConnectorController& connectorController();
    StatusController& statusController();
    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();
    ReloadService& reloadService();

    CollectordTxRouter& txRouter();

    // Pumped by CollectordProcess::tick(), so anything scheduled on it completes on the main loop.
    boost::asio::io_context& ioContext();

private:
    CollectordEventFactory* m_eventFactory;
    CollectordActionFactory* m_actionFactory;
    CollectordTxRouter* m_txRouter;
    boost::asio::io_context* m_ioContext;

    std::unique_ptr<ApiService> m_apiService;
    std::unique_ptr<ConnectorController> m_connectorController;
    std::unique_ptr<StatusController> m_statusController;
    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ReloadService> m_reloadService;

    std::queue<std::unique_ptr<CollectordEvent>> m_eventQueue;
    std::queue<std::unique_ptr<CollectordAction>> m_actionQueue;
};

}
