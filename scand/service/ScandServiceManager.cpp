#include "service/ScandServiceManager.h"

#include "service/api/ApiConnectorTester.h"

#include "config/Config.h"
#include "util/Logger.h"

#include <nlohmann/json.hpp>

#include <chrono>

namespace pz::scand
{

ScandServiceManager::ScandServiceManager(ScandEventFactory* eventFactory, ScandActionFactory* actionFactory,
                                         ScandTxRouter* txRouter, boost::asio::io_context* ioContext)
    : m_eventFactory(eventFactory), m_actionFactory(actionFactory), m_txRouter(txRouter), m_ioContext(ioContext),
      m_apiService(std::make_unique<ApiService>()), m_apiCollector(std::make_unique<ApiCollector>()),
      m_bootstrapService(std::make_unique<BootstrapService>(m_eventFactory, m_actionFactory)),
      m_heartbeatService(std::make_unique<HeartbeatService>()), m_reloadService(std::make_unique<ReloadService>())
{
}

void ScandServiceManager::start()
{
    m_bootstrapService->start();
    m_apiService->start();
}

void ScandServiceManager::schedule()
{
    const auto now = std::chrono::steady_clock::now();

    if (!m_bootstrapService->isReady())
    {
        postEvent(m_bootstrapService->schedule(now));
        return;
    }

    // The issued keys live in engined's api_credential_state; ask once bootstrap is through, so a test
    // or a collection does not need the operator's password again. Not in start(): the IPC
    // client has not registered yet there, and the request would be dropped on the floor.
    // A config reload restarts the daemon, so this runs again with the new configuration.
    if (!m_keysRequested)
    {
        m_keysRequested = true;
        m_apiService->requestKeys(*this);

        // Arm the periodic collection now that the IPC client is live. Each item first fires after
        // its interval, by when the issued keys have arrived; a poll that still finds no key just
        // skips and waits for the next tick.
        m_apiCollector->start(*this, *m_apiService);
    }

    autoRefreshTick(now);
}

// Re-issue key/token for auto-refresh credentials whose interval has elapsed. Reuses the exact
// issuance path a manual Test takes (ApiConnectorTester::run) with a headless ticket (seqNo 0), so
// there is one code path for both. The account credential is opened from the cache engined fed us,
// and the bound device's target/type/fingerprint come from the running config.
void ScandServiceManager::autoRefreshTick(std::chrono::steady_clock::time_point now)
{
    using namespace std::chrono;

    const auto& site = pz::config::Config::serviceSection("engined", "site");
    const auto devices = site.value("devices", nlohmann::json::array());

    for (const auto& p : m_apiService->profiles())
    {
        if (p.refreshMode != "auto")
            continue;

        const auto interval = minutes(p.refreshIntervalMin > 0 ? p.refreshIntervalMin : 60);
        const auto it = m_lastRefresh.find(p.oid);
        if (it != m_lastRefresh.end() && now - it->second < interval)
            continue;

        // The re-issue needs the account credential; if it has not been cached yet (engined's
        // response still in flight), leave the timer unset so the next tick retries promptly.
        const IssuedCredential* cred = m_apiService->credentialFor(p.oid);
        if (!cred)
            continue;

        std::string target, deviceType = "ngfw", fingerprint;
        for (const auto& d : devices)
        {
            if (!d.is_object())
                continue;
            const std::string doid = d.value("oid", d.value("uuid", d.value("id", std::string())));
            if (doid == p.device)
            {
                target = d.value("target", std::string());
                deviceType = d.value("device_type", std::string()) == "sase" ? "sase" : "ngfw";
                fingerprint = d.value("fingerprint", std::string());
                break;
            }
        }
        if (target.empty())
        {
            m_lastRefresh[p.oid] = now;   // misconfigured (no device/target) — retry next interval
            continue;
        }

        m_lastRefresh[p.oid] = now;

        const nlohmann::json input = {
            {"oid", p.oid},
            {"mode", "keygen"},
            {"device_type", deviceType},
            {"target", target},
            {"fingerprint", fingerprint},
            {"endpoint", p.endpoint},
            {"secrets", {{"username", cred->id}, {"password", cred->pw}}},
        };

        LOG_INFO("auto-refresh re-issuing credential (oid={}, type={}, interval_min={})", p.oid, deviceType,
                 p.refreshIntervalMin);
        ApiConnectorTester::run(*m_apiService, *this, 0, input);
    }
}

void ScandServiceManager::postEvent(std::unique_ptr<ScandEvent> event)
{
    if (!event)
    {
        return;
    }

    m_eventQueue.push(std::move(event));
}

void ScandServiceManager::postAction(std::unique_ptr<ScandAction> action)
{
    if (!action)
    {
        return;
    }

    m_actionQueue.push(std::move(action));
}

void ScandServiceManager::execute()
{
    while (!m_eventQueue.empty() or !m_actionQueue.empty())
    {
        if (!m_eventQueue.empty())
        {
            std::unique_ptr<ScandEvent> event = std::move(m_eventQueue.front());
            m_eventQueue.pop();
            event->dispatch(*this);
        }
        else if (!m_actionQueue.empty())
        {
            std::unique_ptr<ScandAction> action = std::move(m_actionQueue.front());
            m_actionQueue.pop();
            action->dispatch(*this);
        }
    }
}

ApiService& ScandServiceManager::apiService()
{
    return *m_apiService;
}

BootstrapService& ScandServiceManager::bootstrapService()
{
    return *m_bootstrapService;
}

HeartbeatService& ScandServiceManager::heartbeatService()
{
    return *m_heartbeatService;
}

ReloadService& ScandServiceManager::reloadService()
{
    return *m_reloadService;
}

ScandTxRouter& ScandServiceManager::txRouter()
{
    return *m_txRouter;
}

boost::asio::io_context& ScandServiceManager::ioContext()
{
    return *m_ioContext;
}

}
