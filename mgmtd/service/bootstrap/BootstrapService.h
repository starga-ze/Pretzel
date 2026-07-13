#pragma once

#include "service/bootstrap/BootstrapEvent.h"
#include "service/bootstrap/BootstrapAction.h"

#include <chrono>
#include <memory>

namespace pz::mgmtd
{

class MgmtdServiceManager;
class MgmtdEventFactory;
class MgmtdActionFactory;

class BootstrapService
{
public:
    // mgmtd is infrastructure (web/admin plane), NOT a config-convergence participant —
    // engined never gates on it (see engined BootstrapService::initProcessMap) and it is
    // not restarted on config reload. Its readiness is therefore the completed IPC
    // handshake alone: Init -> WaitServerHello -> Ready -> Running. It does NOT wait for
    // the fleet-wide RuntimeStart (a signal it isn't part of and would race/miss on a
    // late connect); any RuntimeStart it receives is ignored.
    enum class State
    {
        Init,
        WaitServerHello,
        Ready,
        Running,
        Failed
    };

    BootstrapService(MgmtdEventFactory* eventFactory,
                          MgmtdActionFactory* actionFactory);

    ~BootstrapService() = default;

    void start();

    std::unique_ptr<MgmtdEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(MgmtdServiceManager& serviceManager,
                     const BootstrapEvent& event);

    void handleAction(MgmtdServiceManager& serviceManager,
                      const BootstrapAction& action);

private:
    void onServerHello(MgmtdServiceManager& serviceManager,
                       const pz::ipc::IpcMessage& msg);

    void onRuntimeStart(const pz::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    std::unique_ptr<pz::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<pz::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
};

} // namespace pz::mgmtd
