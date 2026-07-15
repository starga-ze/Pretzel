#pragma once

#include "service/heartbeat/HeartbeatAction.h"
#include "service/heartbeat/HeartbeatEvent.h"

#include "ipc/IpcProtocol.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace pz::engined
{

class EnginedServiceManager;

class HeartbeatService
{
public:
    HeartbeatService() = default;
    ~HeartbeatService() = default;

    void start();

    std::unique_ptr<EnginedEvent> schedule(std::chrono::steady_clock::time_point now);

    void handleEvent(EnginedServiceManager& serviceManager, const HeartbeatEvent& event);

    void handleAction(EnginedServiceManager& serviceManager, const HeartbeatAction& action);

private:
    struct DaemonEntry
    {
        bool pending{false};
        bool alive{false};
        std::int64_t latencyMs{-1};
        std::chrono::steady_clock::time_point sentAt{};
    };

    void onHeartbeatResponse(EnginedServiceManager& serviceManager, const pz::ipc::IpcMessage& msg);

    void markTimeoutsAsDead();

    std::string buildResultJson() const;

    static const std::vector<pz::ipc::IpcDaemon>& targets();
    static const std::vector<pz::ipc::IpcDaemon>& selfReported();

    std::unordered_map<pz::ipc::IpcDaemon, DaemonEntry> m_daemonMap;

    std::chrono::steady_clock::time_point m_lastPollAt{};
    std::chrono::steady_clock::time_point m_roundStartedAt{};

    bool m_roundActive{false};
    int m_pendingCount{0};
};

}
