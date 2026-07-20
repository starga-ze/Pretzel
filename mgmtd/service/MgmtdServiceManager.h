#pragma once

#include "service/ServiceManager.h"

#include "action/MgmtdAction.h"
#include "event/MgmtdEvent.h"

#include "service/auth/AuthService.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/metrics/MetricService.h"
#include "service/web/WebService.h"

#include "router/MgmtdTxRouter.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace pz::mgmtd
{

class MgmtdEventFactory;
class MgmtdActionFactory;

class MgmtdServiceManager : public pz::service::ServiceManager<MgmtdEvent, MgmtdAction>
{
public:
    MgmtdServiceManager(MgmtdEventFactory* eventFactory, MgmtdActionFactory* actionFactory, MgmtdTxRouter* txRouter);
    ~MgmtdServiceManager() override = default;

    void start() override;
    void schedule() override;
    void postEvent(std::unique_ptr<MgmtdEvent> event) override;
    void postAction(std::unique_ptr<MgmtdAction> action) override;
    void execute() override;

    AuthService& authService();
    MetricService& metricService();
    BootstrapService& bootstrapService();
    HeartbeatService& heartbeatService();
    WebService& webService();

    MgmtdTxRouter& txRouter();

    enum class ReloadStatus
    {
        Idle,
        Reloading,
        Complete
    };
    void startReload();
    void completeReload();
    ReloadStatus reloadStatus() const;
    std::int64_t reloadElapsedMs() const;

    void setCommitQueue(std::string snapshotJson);
    std::string commitQueueSnapshot() const;

    void setSsoResult(std::uint32_t ticket, std::string resultJson);
    std::optional<std::string> takeSsoResult(std::uint32_t ticket);

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdActionFactory* m_actionFactory{nullptr};
    MgmtdTxRouter* m_txRouter{nullptr};

    AuthService m_authService;
    MetricService m_metricService;
    std::unique_ptr<BootstrapService> m_bootstrapService;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    WebService m_webService;

    std::queue<std::unique_ptr<MgmtdEvent>> m_eventQueue;
    std::queue<std::unique_ptr<MgmtdAction>> m_actionQueue;

    std::atomic<int> m_reloadStatus{static_cast<int>(ReloadStatus::Idle)};
    std::chrono::steady_clock::time_point m_reloadStartedAt{};

    std::string m_commitQueueSnapshot{"[]"};

    std::unordered_map<std::uint32_t, std::string> m_ssoResults;
};

}
