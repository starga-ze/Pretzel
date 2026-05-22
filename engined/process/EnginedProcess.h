#pragma once

#include "process/Process.h"

#include "ipc/IpcClient.h"
#include "router/TxRouter.h"

#include <chrono>

namespace nf::engined
{

class EnginedProcess : public nf::process::Process
{
public:
    EnginedProcess(nf::ipc::IpcClient* ipcClient, nf::router::TxRouter* txRouter);
    ~EnginedProcess() override = default;

    void start() override;
    void tick() override;

private:
    std::uint32_t nextSeqNo();
    void processIpcHealthCheck();
    void processRuntime();

    nf::ipc::IpcClient* m_ipcClient;
    nf::router::TxRouter* m_txRouter;

    std::chrono::steady_clock::time_point m_lastHealthCheckAt {};
    std::uint32_t m_seqNo {0};
};

} // namespace nf::engined
