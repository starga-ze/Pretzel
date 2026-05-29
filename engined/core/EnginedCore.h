#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "process/EnginedProcess.h"
#include "router/EnginedRxRouter.h"
#include "router/EnginedTxRouter.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace nf::engined
{

class EnginedCore : public nf::core::Core
{
public:
    EnginedCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    nf::config::LoggerConfig m_loggerConfig;
    nf::config::IpcConfig m_ipcConfig;
    
    std::unique_ptr<nf::util::ThreadManager> m_threadManager;
    std::unique_ptr<nf::ipc::IpcClient> m_ipcClient;

    std::unique_ptr<EnginedProcess> m_process;
    std::unique_ptr<EnginedRxRouter> m_rxRouter;
    std::unique_ptr<EnginedTxRouter> m_txRouter;
};

} // namespace nf::engined
