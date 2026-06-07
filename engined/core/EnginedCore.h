#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "process/EnginedProcess.h"
#include "router/EnginedRxRouter.h"
#include "router/EnginedTxRouter.h"
#include "service/EnginedServiceManager.h"
#include "event/EnginedEventFactory.h"
#include "action/EnginedActionFactory.h"

#include "config/ConfigTypes.h"

#include <memory>

namespace pz::engined
{

class EnginedCore : public pz::core::Core
{
public:
    EnginedCore();

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    pz::config::LoggerConfig m_loggerConfig;
    pz::config::IpcConfig m_ipcConfig;

    std::unique_ptr<pz::util::ThreadManager>  m_threadManager;
    std::unique_ptr<pz::ipc::IpcClient>        m_ipcClient;

    std::unique_ptr<EnginedEventFactory>       m_eventFactory;
    std::unique_ptr<EnginedActionFactory>      m_actionFactory;

    std::unique_ptr<EnginedTxRouter>           m_txRouter;
    std::unique_ptr<EnginedServiceManager>     m_serviceManager;
    std::unique_ptr<EnginedRxRouter>           m_rxRouter;

    std::unique_ptr<EnginedProcess>            m_process;
};

} // namespace pz::engined
