#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "process/AuthdProcess.h"
#include "router/AuthdRxRouter.h"
#include "router/AuthdTxRouter.h"
#include "service/AuthdServiceManager.h"
#include "event/AuthdEventFactory.h"
#include "action/AuthdActionFactory.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace nf::authd
{

class AuthdCore : public nf::core::Core
{
public:
    AuthdCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    nf::config::LoggerConfig m_loggerConfig;
    nf::config::IpcConfig m_ipcConfig;

    std::unique_ptr<nf::util::ThreadManager> m_threadManager;
    std::unique_ptr<nf::ipc::IpcClient> m_ipcClient;

    std::unique_ptr<AuthdProcess> m_process;

    std::unique_ptr<AuthdEventFactory> m_eventFactory;
    std::unique_ptr<AuthdActionFactory> m_actionFactory;

    std::unique_ptr<AuthdRxRouter> m_rxRouter;
    std::unique_ptr<AuthdTxRouter> m_txRouter;

    std::unique_ptr<AuthdServiceManager> m_serviceManager;
};

} // namespace nf::authd
