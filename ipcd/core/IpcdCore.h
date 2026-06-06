#pragma once

#include "core/Core.h"

#include "ipc/IpcServer.h"
#include "process/IpcdProcess.h"
#include "router/IpcdRxRouter.h"
#include "router/IpcdTxRouter.h"
#include "service/IpcdServiceManager.h"
#include "event/IpcdEventFactory.h"
#include "action/IpcdActionFactory.h"

#include "config/ConfigTypes.h"
#include "util/ThreadManager.h"

#include <memory>

namespace nf::ipcd
{

using LoggerConfig = nf::config::LoggerConfig;
using IpcConfig = nf::config::IpcConfig;
using ThreadManager = nf::util::ThreadManager;

class IpcdCore : public nf::core::Core
{
public:
    IpcdCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    LoggerConfig m_loggerConfig;
    IpcConfig m_ipcConfig;

    std::unique_ptr<ThreadManager> m_threadManager;
    std::unique_ptr<IpcServer> m_ipcServer;

    std::unique_ptr<IpcdEventFactory>   m_eventFactory;
    std::unique_ptr<IpcdActionFactory>  m_actionFactory;

    std::unique_ptr<IpcdTxRouter>       m_txRouter;
    std::unique_ptr<IpcdServiceManager> m_serviceManager;
    std::unique_ptr<IpcdRxRouter>       m_rxRouter;

    std::unique_ptr<IpcdProcess> m_process;
};

} // namespace nf::ipcd
