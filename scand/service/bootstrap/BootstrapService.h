#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include <chrono>
#include <memory>

namespace pz::scand
{

class ScandServiceManager;
class ScandEventFactory;
class ScandActionFactory;

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
        Failed,
    };

    BootstrapService(ScandEventFactory* eventFactory,
                     ScandActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<ScandEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(ScandServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(ScandServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    void onServerHello(ScandServiceManager& serviceManager,
                       const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    ScandEventFactory* m_eventFactory{nullptr};
    ScandActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};

    bool m_bootSlowWarned{false};  // warn-once latch for the slow-boot message
};

} // namespace pz::scand
