#pragma once

#include "service/ServiceManager.h"

#include "event/MgmtdEvent.h"
#include "action/MgmtdAction.h"

#include "service/auth/AuthService.h"
#include "service/bootstrap/MgmtdBootstrapService.h"
#include "service/metrics/MetricService.h"
#include "service/probe/MgmtdProbeService.h"

#include "router/MgmtdTxRouter.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>

namespace nf::mgmtd
{

class MgmtdEventFactory;
class MgmtdActionFactory;

class MgmtdServiceManager : public nf::service::ServiceManager<MgmtdEvent, MgmtdAction>
{
public:
    MgmtdServiceManager(MgmtdEventFactory* eventFactory,
                        MgmtdActionFactory* actionFactory,
                        MgmtdTxRouter* txRouter);
    ~MgmtdServiceManager() override = default;

    void start() override;
    void schedule() override;
    void postEvent(std::unique_ptr<MgmtdEvent> event) override;
    void postAction(std::unique_ptr<MgmtdAction> action) override;
    void execute() override;

    AuthService& authService();
    MetricService& metricService();
    MgmtdBootstrapService& bootstrapService();
    MgmtdProbeService& probeService();

    MgmtdTxRouter& txRouter();

    std::optional<std::uint32_t> aliveDevices() const;
    void setAliveDevices(std::uint32_t count);

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdActionFactory* m_actionFactory{nullptr};
    MgmtdTxRouter* m_txRouter{nullptr};

    AuthService m_authService;
    MetricService m_metricService;
    std::unique_ptr<MgmtdBootstrapService> m_bootstrapService;
    std::unique_ptr<MgmtdProbeService> m_probeService;

    std::queue<std::unique_ptr<MgmtdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<MgmtdAction>> m_actionQueue;

    std::atomic<std::int64_t> m_aliveDevices{-1};
};

} // namespace nf::mgmtd
