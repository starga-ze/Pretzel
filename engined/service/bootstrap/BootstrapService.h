#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include "ipc/IpcProtocol.h"

#include <chrono>
#include <memory>
#include <unordered_map>

namespace pz::engined
{

class EnginedServiceManager;
class EnginedEventFactory;
class EnginedActionFactory;

class BootstrapService
{
public:
    // Reconciliation model. After the one-time engined<->ipcd handshake, engined sits
    // in Reconcile forever: every tick it refreshes the runtime-table snapshot
    // (SyncRequest) and drives the fleet toward m_targetVersion. There is no terminal
    // state — boot, reload, and a lone daemon crash-restart are all just "actual state
    // drifted from desired; converge again."
    enum class State
    {
        Init,
        WaitHandshake,
        Reconcile
    };

    BootstrapService(EnginedEventFactory* eventFactory,
                            EnginedActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();
    void scheduleServiceReload();

    std::unique_ptr<EnginedEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(EnginedServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(EnginedServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    void onServerHello(EnginedServiceManager& serviceManager,
                       const pz::ipc::IpcMessage& msg);

    void onSyncResponse(EnginedServiceManager& serviceManager,
                        const pz::ipc::IpcMessage& msg);

    void warnIfBootSlow(std::chrono::steady_clock::time_point now,
                        const char* stateName);

    void initProcessMap();
    bool updateProcessMap(const pz::ipc::IpcMessage& msg);
    bool isProcessReady(pz::ipc::IpcDaemon daemon) const;
    bool isAllProcessReady() const;
    void dumpProcessMap() const;

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildSyncRequestMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeStartMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildConfigReloadResponse(bool ok) const;

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastSyncRequestSentAt{};
    std::chrono::steady_clock::time_point m_lastBootWarnAt{};

    // Per-daemon readiness by config-version convergence. A daemon is "ready" once it
    // has reported applying a running-config version >= m_targetVersion. This is
    // level-triggered and idempotent: unlike the former generation-counting scheme it
    // does not depend on observing a disconnect/reconnect edge, so duplicate or
    // out-of-order SyncResponses are harmless.
    //   connected:         present in the latest SyncResponse (fd>=0 at ipcd). Required
    //                      so a target of 0 (DB-down boot) can never read as converged
    //                      before the daemon has even connected.
    //   appliedVersion:    latest running-config version the daemon reported applying.
    //   generation:        ipcd connection epoch — bumped on every (re)connect.
    //   blessedGeneration: the generation for which engined last sent this daemon a
    //                      RuntimeStart. When generation > blessedGeneration the daemon
    //                      has (re)connected since it was last cleared to run, so once
    //                      it is back at the target version engined re-sends RuntimeStart.
    //                      This is what self-heals a lone crash-restart (scenario 3).
    struct ProcessState
    {
        bool     connected         = false;
        uint64_t appliedVersion    = 0;
        uint32_t generation        = 0;
        uint32_t blessedGeneration = 0;
    };

    // The running-config version the fleet must converge to. Set at cold start to the
    // version engined loaded, and bumped to the newly-committed version on reload.
    uint64_t m_targetVersion{0};

    std::unordered_map<pz::ipc::IpcDaemon, ProcessState> m_processMap;

    bool m_isReload{false};      // true while a commit-triggered reload is converging
    bool m_bootstrapped{false};  // latched true once the initial convergence completes;
                                 // gates engined's own services via isReady()

    std::chrono::steady_clock::time_point m_reloadStartedAt{};  // for reloadTimeout
};

} // namespace pz::engined
