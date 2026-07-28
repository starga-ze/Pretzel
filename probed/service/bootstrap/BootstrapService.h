#pragma once

#include "service/bootstrap/BootstrapAction.h"
#include "service/bootstrap/BootstrapEvent.h"

#include <chrono>
#include <memory>

namespace pz::probed
{

class ProbedServiceManager;
class ProbedEventFactory;
class ProbedActionFactory;

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

    BootstrapService(ProbedEventFactory* eventFactory, ProbedActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<ProbedEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(ProbedServiceManager& serviceManager, const BootstrapEvent& event);

    void handleAction(ProbedServiceManager& serviceManager, const BootstrapAction& action);

private:
    void onServerHello(ProbedServiceManager& serviceManager, const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now, const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    ProbedEventFactory* m_eventFactory{nullptr};
    ProbedActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};

    bool m_bootSlowWarned{false};
};

}
