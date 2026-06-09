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
    enum class State
    {
        Init,
        WaitHandshake,
        WaitSync,
        Ready,
        Running,
        Failed
    };

    BootstrapService(EnginedEventFactory* eventFactory,
                            EnginedActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    // Called when engined receives ConfigReloadRequest from mgmtd.
    // Re-enters WaitSync without restarting the process: engined invalidates
    // its own config cache in-place and waits for service daemons to cycle
    // through disconnect → reconnect before sending a new RuntimeStart.
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

    void onSyncResponse(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    void initProcessMap();
    bool updateProcessMap(const pz::ipc::IpcMessage& msg);
    bool isProcessReady(pz::ipc::IpcDaemon daemon) const;
    bool isAllProcessReady() const;
    void dumpProcessMap() const;

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildSyncRequestMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeStartMessage() const;

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastSyncRequestSentAt{};

    // Per-daemon readiness tracked by connection generation.
    // knownGeneration: last generation seen in a SyncResponse.
    // requiredGeneration: minimum generation to accept as "ready".
    //   - Cold start: 0 (accept any generation, even the very first connect).
    //   - Reload: knownGeneration+1 at the time of reload (requires a fresh
    //     reconnect after the ConfigReload was dispatched).
    struct ProcessState
    {
        bool     ready             = false;
        uint32_t knownGeneration   = 0;
        uint32_t requiredGeneration = 0;
    };

    std::unordered_map<pz::ipc::IpcDaemon, ProcessState> m_processMap;
};

} // namespace pz::engined
