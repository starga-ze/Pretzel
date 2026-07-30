#pragma once

#include "service/ServiceManager.h"

#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/status/icmp/IcmpService.h"
#include "service/reload/ReloadService.h"

#include "router/ProbedTxRouter.h"

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <queue>
#include <unordered_map>

namespace pz::probed
{

class ProbedServiceManager : public pz::service::ServiceManager<ProbedEvent, ProbedAction>
{
public:
    ProbedServiceManager(ProbedEventFactory* eventFactory, ProbedActionFactory* actionFactory,
                         ProbedTxRouter* txRouter, boost::asio::io_context* ioContext);
    ~ProbedServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<ProbedEvent> event) override;
    void postAction(std::unique_ptr<ProbedAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    IcmpService& icmpService();
    HeartbeatService& heartbeatService();
    ReloadService& reloadService();

    ProbedTxRouter& txRouter();
    boost::asio::io_context& ioContext();

private:
    ProbedEventFactory* m_eventFactory;
    ProbedActionFactory* m_actionFactory;
    ProbedTxRouter* m_txRouter;
    boost::asio::io_context* m_ioContext;

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<IcmpService> m_icmpService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ReloadService> m_reloadService;

    std::queue<std::unique_ptr<ProbedEvent>> m_eventQueue;
    std::queue<std::unique_ptr<ProbedAction>> m_actionQueue;
};

}
