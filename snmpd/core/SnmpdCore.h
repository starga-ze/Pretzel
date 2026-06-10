#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "process/SnmpdProcess.h"
#include "router/SnmpdRxRouter.h"
#include "router/SnmpdTxRouter.h"
#include "service/SnmpdServiceManager.h"
#include "event/SnmpdEventFactory.h"
#include "action/SnmpdActionFactory.h"
#include "snmp/SnmpEngine.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace pz::snmpd
{

class SnmpdCore : public pz::core::Core
{
public:
    SnmpdCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    pz::config::LoggerConfig m_loggerConfig;
    pz::config::IpcConfig m_ipcConfig;

    std::unique_ptr<pz::util::ThreadManager> m_threadManager;
    std::unique_ptr<pz::ipc::IpcClient> m_ipcClient;

    std::unique_ptr<SnmpEngine> m_snmpEngine;

    std::unique_ptr<SnmpdProcess> m_process;

    std::unique_ptr<SnmpdEventFactory> m_eventFactory;
    std::unique_ptr<SnmpdActionFactory> m_actionFactory;

    std::unique_ptr<SnmpdRxRouter> m_rxRouter;
    std::unique_ptr<SnmpdTxRouter> m_txRouter;

    std::unique_ptr<SnmpdServiceManager> m_serviceManager;
};

} // namespace pz::snmpd
