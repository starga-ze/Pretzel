#pragma once

#include "service/ServiceManager.h"

#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"

#include "router/SnmpdTxRouter.h"

#include <queue>

namespace nf::snmpd
{

class SnmpdEventFactory;
class SnmpdActionFactory;

class SnmpdServiceManager : public nf::service::ServiceManager<SnmpdEvent, SnmpdAction>
{
public:
    SnmpdServiceManager(SnmpdEventFactory* eventFactory,
                        SnmpdActionFactory* actionFactory,
                        SnmpdTxRouter* txRouter);
    ~SnmpdServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<SnmpdEvent> event) override;
    void postAction(std::unique_ptr<SnmpdAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();

    SnmpdTxRouter& txRouter();

private:
    SnmpdEventFactory* m_eventFactory;
    SnmpdActionFactory* m_actionFactory;
    SnmpdTxRouter* m_txRouter;

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;

    std::queue<std::unique_ptr<SnmpdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<SnmpdAction>> m_actionQueue;
};

} // namespace nf::snmpd
