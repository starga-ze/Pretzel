#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "process/IcmpdProcess.h"
#include "router/IcmpdRxRouter.h"
#include "router/IcmpdTxRouter.h"
#include "service/IcmpdServiceManager.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace nf::icmpd
{

class IcmpdCore : public nf::core::Core
{
public:
    IcmpdCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    nf::config::LoggerConfig m_loggerConfig;
    nf::config::IpcConfig m_ipcConfig;

    std::unique_ptr<nf::util::ThreadManager> m_threadManager;
    std::unique_ptr<nf::ipc::IpcClient> m_ipcClient;

    std::unique_ptr<IcmpdProcess> m_process;
    std::unique_ptr<IcmpdRxRouter> m_rxRouter;
    std::unique_ptr<IcmpdTxRouter> m_txRouter;

    std::unique_ptr<IcmpdServiceManager> m_serviceManager;
};

} // namespace nf::icmpd
