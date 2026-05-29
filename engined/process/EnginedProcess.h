#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "router/EnginedTxRouter.h"

#include <chrono>
#include <unordered_map>
#include <vector>

namespace nf::engined
{

enum class BootstrapState
{
    Init,
    WaitHandshake,
    WaitSync,
    Ready,
    Running,
    Failed
};

class EnginedProcess : public nf::process::Process
{
public:
    EnginedProcess(nf::ipc::IpcClient* ipcClient, EnginedTxRouter* txRouter);
    ~EnginedProcess() override = default;

    bool start() override;
    void tick() override;

    void onServerHello(const nf::ipc::IpcMessage& msg);
    void onSync(const nf::ipc::IpcMessage& msg);

private:
    void processBootstrap();
    void processRuntime();

    bool checkBootstrapTimeout(std::chrono::steady_clock::time_point now, const char* state);

    bool updateProcessMap(const nf::ipc::IpcMessage& msg);
    void initProcessMap();
    void dumpProcessMap() const;
    bool isProcessReady(nf::ipc::IpcDaemon daemon) const;
    bool isAllProcessReady() const;

    nf::ipc::IpcClient* m_ipcClient;
    EnginedTxRouter* m_txRouter;

    BootstrapState m_bootstrapState { BootstrapState::Init };

    std::chrono::steady_clock::time_point m_bootstrapStartAt {};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt {};
    std::chrono::steady_clock::time_point m_lastSyncRequestSentAt {};

    std::unordered_map<nf::ipc::IpcDaemon, bool> m_processMap;
};

} // namespace nf::engined
