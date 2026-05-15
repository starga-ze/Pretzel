#pragma once

#include "core/Core.h"

#include "process/EnginedProcess.h"
#include "ipc/IpcClient.h"
#include "router/EnginedRxRouter.h"
#include "router/EnginedTxRouter.h"

#include "config/ConfigTypes.h"
#include "util/ThreadManager.h"

#include <memory>

namespace nf::engined
{

using IpcClient = nf::ipc::IpcClient;
using LoggerConfig = nf::config::LoggerConfig;
using IpcConfig = nf::config::IpcConfig;
using ThreadManager = nf::util::ThreadManager;

class CoreEngine : public nf::core::Core
{
public:
    CoreEngine();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    LoggerConfig m_loggerConfig;
    IpcConfig m_ipcConfig;
    
    std::unique_ptr<ThreadManager> m_threadManager;
    std::unique_ptr<IpcClient> m_ipcClient;

    std::unique_ptr<EnginedRxRouter> m_rxRouter;
    std::unique_ptr<EnginedTxRouter> m_txRouter;

    std::unique_ptr<EnginedProcess> m_process;
};

} // namespace nf::engined
