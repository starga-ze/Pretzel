#pragma once

#include "service/ServiceManager.h"

#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/reload/ReloadService.h"

#include "router/AuthdTxRouter.h"

#include <queue>

namespace pz::authd
{

class AuthdEventFactory;
class AuthdActionFactory;

class AuthdServiceManager : public pz::service::ServiceManager<AuthdEvent, AuthdAction>
{
public:
    AuthdServiceManager(AuthdEventFactory* eventFactory,
                        AuthdActionFactory* actionFactory,
                        AuthdTxRouter* txRouter);
    ~AuthdServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<AuthdEvent> event) override;
    void postAction(std::unique_ptr<AuthdAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();
    ReloadService& reloadService();

    AuthdTxRouter& txRouter();

private:
    AuthdEventFactory* m_eventFactory;
    AuthdActionFactory* m_actionFactory;
    AuthdTxRouter* m_txRouter;

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ReloadService> m_reloadService;

    std::queue<std::unique_ptr<AuthdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<AuthdAction>> m_actionQueue;
};

} // namespace pz::authd
