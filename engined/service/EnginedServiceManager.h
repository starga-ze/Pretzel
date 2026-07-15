#pragma once

#include "service/ServiceManager.h"

#include "action/EnginedAction.h"
#include "event/EnginedEvent.h"

#include "service/admin/AdminService.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/commit/CommitService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/probe/ProbeService.h"
#include "service/scan/ScanService.h"

#include "vendor/VendorResolver.h"

#include "router/EnginedTxRouter.h"

#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace pz::engined
{

class EnginedEventFactory;
class EnginedActionFactory;

class EnginedServiceManager : public pz::service::ServiceManager<EnginedEvent, EnginedAction>
{
public:
    EnginedServiceManager(EnginedEventFactory* eventFactory, EnginedActionFactory* actionFactory,
                          EnginedTxRouter* txRouter);
    ~EnginedServiceManager() override = default;

    void start() override;
    void schedule() override;
    void postEvent(std::unique_ptr<EnginedEvent> event) override;
    void postAction(std::unique_ptr<EnginedAction> action) override;
    void execute() override;

    BootstrapService& bootstrapService();
    CommitService& commitService();
    HeartbeatService& heartbeatService();
    ScanService& scanService();
    ProbeService& probeService();
    AdminService& adminService();
    VendorResolver& vendorResolver();

    EnginedTxRouter& txRouter();
    EnginedEventFactory* eventFactory();
    EnginedActionFactory* actionFactory();

    const std::vector<std::string>& aliveIps() const;
    void setAliveIps(std::vector<std::string> ips);

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedActionFactory* m_actionFactory{nullptr};
    EnginedTxRouter* m_txRouter{nullptr};

    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<CommitService> m_commitService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<ScanService> m_scanService;
    std::unique_ptr<ProbeService> m_probeService;
    std::unique_ptr<AdminService> m_adminService;
    std::unique_ptr<VendorResolver> m_vendorResolver;

    std::vector<std::string> m_aliveIps;

    std::queue<std::unique_ptr<EnginedEvent>> m_eventQueue;
    std::queue<std::unique_ptr<EnginedAction>> m_actionQueue;
};

}
