#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "process/ScandProcess.h"
#include "router/ScandRxRouter.h"
#include "router/ScandTxRouter.h"
#include "service/ScandServiceManager.h"
#include "event/ScandEventFactory.h"
#include "action/ScandActionFactory.h"
#include "snmp/SnmpEngine.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace pz::scand
{

class ScandCore : public pz::core::Core
{
public:
    ScandCore();

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

    std::unique_ptr<ScandProcess> m_process;

    std::unique_ptr<ScandEventFactory> m_eventFactory;
    std::unique_ptr<ScandActionFactory> m_actionFactory;

    std::unique_ptr<ScandRxRouter> m_rxRouter;
    std::unique_ptr<ScandTxRouter> m_txRouter;

    std::unique_ptr<ScandServiceManager> m_serviceManager;
};

} // namespace pz::scand
