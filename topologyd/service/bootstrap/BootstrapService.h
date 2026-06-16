#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include "ipc/IpcProtocol.h"

#include <chrono>
#include <memory>

namespace pz::topologyd
{

class TopologydServiceManager;
class TopologydEventFactory;
class TopologydActionFactory;

class BootstrapService
{
public:
    enum class State
    {
        Init,
        WaitServerHello,
        WaitRuntimeStart,
        Ready,
        Running,
        Failed
    };

    BootstrapService(TopologydEventFactory* eventFactory,
                     TopologydActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<TopologydEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(TopologydServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(TopologydServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    void onServerHello(TopologydServiceManager& serviceManager,
                       const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    TopologydEventFactory*  m_eventFactory{nullptr};
    TopologydActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};

    bool m_bootSlowWarned{false};  // warn-once latch for the slow-boot message
};

} // namespace pz::topologyd
