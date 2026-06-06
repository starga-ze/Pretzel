#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include <chrono>
#include <memory>

namespace nf::snmpd
{

class SnmpdServiceManager;
class SnmpdEventFactory;
class SnmpdActionFactory;

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

    BootstrapService(SnmpdEventFactory* eventFactory,
                     SnmpdActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<SnmpdEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(SnmpdServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(SnmpdServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    void onServerHello(SnmpdServiceManager& serviceManager,
                       const nf::ipc::IpcMessage& msg);

    void onRuntimeStart(const nf::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    std::unique_ptr<nf::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<nf::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    SnmpdEventFactory* m_eventFactory{nullptr};
    SnmpdActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};
};

} // namespace nf::snmpd
