#pragma once

#include "core/Core.h"

#include "icmp/IcmpEngine.h"
#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "action/IcmpdActionFactory.h"
#include "event/IcmpdEventFactory.h"
#include "process/IcmpdProcess.h"
#include "router/IcmpdRxRouter.h"
#include "router/IcmpdTxRouter.h"
#include "service/IcmpdServiceManager.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace pz::icmpd
{

class IcmpdCore : public pz::core::Core
{
public:
    IcmpdCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    pz::config::LoggerConfig m_loggerConfig;
    pz::config::IpcConfig m_ipcConfig;

    std::unique_ptr<pz::util::ThreadManager> m_threadManager;
    std::unique_ptr<pz::ipc::IpcClient> m_ipcClient;
    std::unique_ptr<IcmpEngine> m_icmpEngine;

    std::unique_ptr<IcmpdProcess> m_process;

    std::unique_ptr<IcmpdEventFactory> m_eventFactory;
    std::unique_ptr<IcmpdActionFactory> m_actionFactory;

    std::unique_ptr<IcmpdRxRouter> m_rxRouter;
    std::unique_ptr<IcmpdTxRouter> m_txRouter;

    std::unique_ptr<IcmpdServiceManager> m_serviceManager;
};

}
