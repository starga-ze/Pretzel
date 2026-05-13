#pragma once

#include "core/Core.h"
#include "util/ThreadManager.h"
#include "ipc/IpcServer.h"
#include "router/IpcdRxRouter.h"
#include "router/IpcdTxRouter.h"
#include "config/ConfigTypes.h"

#include <memory>

namespace nf::ipcd
{
    
using LoggerConfig = nf::config::LoggerConfig;
using IpcConfig = nf::config::IpcConfig;
using ThreadManager = nf::util::ThreadManager;

class CoreIpc : public nf::core::Core
{
public:
    CoreIpc();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    void initConfig();
    void initLogger();
    bool initIpcRuntime();
    void processRuntime();

    LoggerConfig m_loggerConfig;
    IpcConfig m_ipcConfig;
    
    std::unique_ptr<IpcServer> m_ipcServer;

    std::unique_ptr<IpcdRxRouter> m_rxRouter;
    std::unique_ptr<IpcdTxRouter> m_txRouter;
};

} // namespace nf::ipcd
