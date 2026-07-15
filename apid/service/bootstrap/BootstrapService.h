#pragma once

#include "service/bootstrap/BootstrapAction.h"
#include "service/bootstrap/BootstrapEvent.h"

#include <chrono>
#include <memory>

namespace pz::apid
{

class ApidServiceManager;
class ApidEventFactory;
class ApidActionFactory;

class BootstrapService
{
public:
    enum class State
    {
        Init,
        WaitServerHello,
        Ready,
        Running,
        Failed
    };

    BootstrapService(ApidEventFactory* eventFactory, ApidActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<ApidEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(ApidServiceManager& serviceManager, const BootstrapEvent& event);

    void handleAction(ApidServiceManager& serviceManager, const BootstrapAction& action);

private:
    void onServerHello(ApidServiceManager& serviceManager, const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    void warnIfBootSlow(std::chrono::steady_clock::time_point now, const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    ApidEventFactory* m_eventFactory{nullptr};
    ApidActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    bool m_bootSlowWarned{false};
};

}
