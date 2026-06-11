#pragma once

#include "service/ServiceManager.h"

#include "event/MgmtdEvent.h"
#include "action/MgmtdAction.h"

#include "service/auth/AuthService.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/metrics/MetricService.h"
#include "service/probe/ProbeService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/snmp/SnmpService.h"

#include "router/MgmtdTxRouter.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace pz::mgmtd
{

class MgmtdEventFactory;
class MgmtdActionFactory;

class MgmtdServiceManager : public pz::service::ServiceManager<MgmtdEvent, MgmtdAction>
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
    BootstrapService& bootstrapService();
    ProbeService& probeService();
    HeartbeatService& heartbeatService();
    SnmpService& snmpService();

    MgmtdTxRouter& txRouter();

    std::optional<std::uint32_t> aliveDevices() const;
    void setAliveDevices(std::uint32_t count);

    std::vector<std::string> aliveIps() const;
    void setAliveIps(std::vector<std::string> ips);

    enum class ReloadStatus { Idle, Reloading, Complete };
    void startReload();
    void completeReload();
    ReloadStatus reloadStatus() const;
    std::int64_t reloadElapsedMs() const;

    void        setCommitQueue(std::string snapshotJson);
    std::string commitQueueSnapshot() const;

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdActionFactory* m_actionFactory{nullptr};
    MgmtdTxRouter* m_txRouter{nullptr};

    AuthService m_authService;
    MetricService m_metricService;
    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<ProbeService> m_probeService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<SnmpService> m_snmpService;

    std::queue<std::unique_ptr<MgmtdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<MgmtdAction>> m_actionQueue;

    std::atomic<std::int64_t>  m_aliveDevices{-1};
    std::vector<std::string>   m_aliveIps;
    std::atomic<int>           m_reloadStatus{static_cast<int>(ReloadStatus::Idle)};
    std::chrono::steady_clock::time_point m_reloadStartedAt{};

    std::string        m_commitQueueSnapshot{"[]"};
};

} // namespace pz::mgmtd
