#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include <chrono>
#include <memory>

namespace pz::authd
{

class AuthdServiceManager;
class AuthdEventFactory;
class AuthdActionFactory;

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

    BootstrapService(AuthdEventFactory* eventFactory,
                     AuthdActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<AuthdEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(AuthdServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(AuthdServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    void onServerHello(AuthdServiceManager& serviceManager,
                       const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    AuthdEventFactory* m_eventFactory{nullptr};
    AuthdActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};

    bool m_bootSlowWarned{false};  // warn-once latch for the slow-boot message
};

} // namespace pz::authd
