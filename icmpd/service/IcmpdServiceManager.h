#pragma once

#include "service/ServiceManager.h"

#include "service/bootstrap/BootstrapService.h"
#include "service/probe/ProbeService.h"
#include "service/heartbeat/HeartbeatService.h"

#include "router/IcmpdTxRouter.h"

#include <queue>

namespace pz::icmpd
{

class IcmpdServiceManager : public pz::service::ServiceManager<IcmpdEvent, IcmpdAction>
{
public:
    IcmpdServiceManager(IcmpdEventFactory* eventFactory, 
            IcmpdActionFactory* actionFactory, 
            IcmpdTxRouter* txRouter);
    ~IcmpdServiceManager() override = default;

    void start() override;

    void schedule() override;
    void postEvent(std::unique_ptr<IcmpdEvent> event) override;
    void postAction(std::unique_ptr<IcmpdAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    ProbeService& probeService();
    HeartbeatService& heartbeatService();

    IcmpdTxRouter& txRouter();

private:
    IcmpdEventFactory* m_eventFactory;
    IcmpdActionFactory* m_actionFactory;   
    IcmpdTxRouter* m_txRouter;

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<ProbeService> m_probeService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;

    std::queue<std::unique_ptr<IcmpdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<IcmpdAction>> m_actionQueue;
};

} // namespace pz::icmpd
