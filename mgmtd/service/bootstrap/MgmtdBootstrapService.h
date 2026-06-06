#pragma once

#include "service/bootstrap/MgmtdBootstrapEvent.h"
#include "service/bootstrap/MgmtdBootstrapAction.h"

#include <chrono>
#include <memory>

namespace nf::mgmtd
{

class MgmtdServiceManager;
class MgmtdEventFactory;
class MgmtdActionFactory;

class MgmtdBootstrapService
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

    MgmtdBootstrapService(MgmtdEventFactory* eventFactory,
                          MgmtdActionFactory* actionFactory);

    ~MgmtdBootstrapService() = default;

    void start();

    std::unique_ptr<MgmtdEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(MgmtdServiceManager& serviceManager,
                     const MgmtdBootstrapEvent& event);

    void handleAction(MgmtdServiceManager& serviceManager,
                      const MgmtdBootstrapAction& action);

private:
    void onServerHello(MgmtdServiceManager& serviceManager,
                       const nf::ipc::IpcMessage& msg);

    void onRuntimeStart(const nf::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    std::unique_ptr<nf::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<nf::ipc::IpcMessage> buildRuntimeReadyMessage() const;

private:
    MgmtdEventFactory* m_eventFactory{nullptr};
    MgmtdActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};
};

} // namespace nf::mgmtd
