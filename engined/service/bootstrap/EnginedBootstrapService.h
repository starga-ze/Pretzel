#pragma once

#include "service/bootstrap/EnginedBootstrapEvent.h"
#include "service/bootstrap/EnginedBootstrapAction.h"

#include "ipc/IpcProtocol.h"

#include <chrono>
#include <memory>
#include <unordered_map>

namespace nf::engined
{

class EnginedServiceManager;
class EnginedEventFactory;
class EnginedActionFactory;

class EnginedBootstrapService
{
public:
    enum class State
    {
        Init,
        WaitHandshake,
        WaitSync,
        Ready,
        Running,
        Failed
    };

    EnginedBootstrapService(EnginedEventFactory* eventFactory,
                            EnginedActionFactory* actionFactory);

    ~EnginedBootstrapService() = default;

    void start();

    std::unique_ptr<EnginedEvent> schedule(std::chrono::steady_clock::time_point now);

    bool isReady() const;

    void handleEvent(EnginedServiceManager& serviceManager,
                     const EnginedBootstrapEvent& event);

    void handleAction(EnginedServiceManager& serviceManager,
                      const EnginedBootstrapAction& action);

private:
    void onServerHello(EnginedServiceManager& serviceManager,
                       const nf::ipc::IpcMessage& msg);

    void onSyncResponse(const nf::ipc::IpcMessage& msg);

    bool checkTimeout(std::chrono::steady_clock::time_point now,
                      const char* stateName);

    void initProcessMap();
    bool updateProcessMap(const nf::ipc::IpcMessage& msg);
    bool isProcessReady(nf::ipc::IpcDaemon daemon) const;
    bool isAllProcessReady() const;
    void dumpProcessMap() const;

    std::unique_ptr<nf::ipc::IpcMessage> buildClientHelloMessage() const;
    std::unique_ptr<nf::ipc::IpcMessage> buildSyncRequestMessage() const;
    std::unique_ptr<nf::ipc::IpcMessage> buildRuntimeStartMessage() const;

private:
    EnginedEventFactory* m_eventFactory{nullptr};
    EnginedActionFactory* m_actionFactory{nullptr};

    State m_state{State::Init};

    std::chrono::steady_clock::time_point m_startedAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastSyncRequestSentAt{};

    std::unordered_map<nf::ipc::IpcDaemon, bool> m_processMap;
};

} // namespace nf::engined
