#pragma once

#include "service/ServiceManager.h"

#include "action/MgmtdAction.h"
#include "event/MgmtdEvent.h"

#include "service/auth/AuthService.h"
#include "service/bootstrap/BootstrapService.h"
#include "service/heartbeat/HeartbeatService.h"
#include "service/metrics/MetricService.h"
#include "service/web/WebService.h"

#include "http/StaticFileCache.h"

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

    // API connector tests reach out to a customer device, which may be slow or unreachable, so
    // scand performs them and answers over IPC. Both ends of this store are on the main loop —
    // MgmtdRxRouter fills it, the polling web handler drains it — so no lock is needed.
    void setApiTestResult(std::uint32_t ticket, std::string resultJson);
    std::optional<std::string> takeApiTestResult(std::uint32_t ticket);

    // Static file cache, set once at startup by MgmtdCore. The web handlers read it from here so
    // they can stay stateless (plain functions in the route table).
    void setStaticCache(std::shared_ptr<pz::http::StaticFileCache> cache);
    const std::shared_ptr<pz::http::StaticFileCache>& staticCache() const;

    // Monotonic ticket ids the web handlers hand to the browser to poll an async result on. Kept
    // here beside the result stores they key into, not on the (now stateless) WebService.
    std::uint32_t nextSsoTicket();
    std::uint32_t nextApiTestTicket();

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

    std::unordered_map<std::uint32_t, std::string> m_apiTestResults;

    std::shared_ptr<pz::http::StaticFileCache> m_staticCache;
    std::uint32_t m_ssoTicket{1};
    std::uint32_t m_apiTestTicket{1};
};

}
