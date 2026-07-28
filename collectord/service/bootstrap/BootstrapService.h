#pragma once

#include "service/bootstrap/BootstrapAction.h"
#include "service/bootstrap/BootstrapEvent.h"

#include <chrono>
#include <memory>

namespace pz::collectord
{

class CollectordServiceManager;
class CollectordEventFactory;
class CollectordActionFactory;

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

    BootstrapService(CollectordEventFactory* eventFactory, CollectordActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<CollectordEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(CollectordServiceManager& serviceManager, const BootstrapEvent& event);

    void handleAction(CollectordServiceManager& serviceManager, const BootstrapAction& action);

private:
    void onServerHello(CollectordServiceManager& serviceManager, const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now, const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    CollectordEventFactory* m_eventFactory{nullptr};
    CollectordActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};

    bool m_bootSlowWarned{false};
};

}
