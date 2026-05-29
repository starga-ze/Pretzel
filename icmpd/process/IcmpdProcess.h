#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "router/IcmpdTxRouter.h"

#include <chrono>

namespace nf::icmpd
{

enum class BootstrapState
{
    Init,
    WaitHandshake,
    WaitRuntime,
    Ready,
    Running,
    Failed
};

class IcmpdProcess : public nf::process::Process
{
public:
    IcmpdProcess(nf::ipc::IpcClient* ipcClient, IcmpdTxRouter* txRouter);
    ~IcmpdProcess() override = default;

    bool start() override;
    void tick() override;

    void onServerHello(const nf::ipc::IpcMessage& msg);
    void onRuntimeStart(const nf::ipc::IpcMessage& msg);

private:
    void processBootstrap();
    void processRuntime();

    bool checkBootstrapTimeout(std::chrono::steady_clock::time_point now, const char* state);

    nf::ipc::IpcClient* m_ipcClient;
    IcmpdTxRouter* m_txRouter;

    BootstrapState m_bootstrapState{BootstrapState::Init};

    std::chrono::steady_clock::time_point m_bootstrapStartAt{};
    std::chrono::steady_clock::time_point m_lastClientHelloSentAt{};
    std::chrono::steady_clock::time_point m_lastRuntimeReadySentAt{};
};

} // namespace nf::icmpd
