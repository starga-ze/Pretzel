#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "router/EnginedTxRouter.h"

#include <chrono>

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

    nf::ipc::IpcClient* m_ipcClient;
    EnginedTxRouter* m_txRouter;

    BootstrapState m_bootstrapState { BootstrapState::Init };

    std::chrono::steady_clock::time_point m_bootstrapStartAt {};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt {};
};

} // namespace nf::engined
