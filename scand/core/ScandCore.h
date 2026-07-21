#pragma once

#include "core/Core.h"

#include "ipc/IpcClient.h"
#include "util/ThreadManager.h"

#include "action/ScandActionFactory.h"
#include "event/ScandEventFactory.h"
#include "process/ScandProcess.h"
#include "router/ScandRxRouter.h"
#include "router/ScandTxRouter.h"
#include "service/ScandServiceManager.h"

#include "config/ConfigTypes.h"

#include <boost/asio/io_context.hpp>

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

    // Drives outbound device calls (pz::http::requestAsync). scand serves no HTTP of its own,
    // so unlike mgmtd there is no HttpServer to host the context — it is pumped directly from
    // ScandProcess::tick(), and every completion handler therefore runs on the main loop.
    std::unique_ptr<boost::asio::io_context> m_ioContext;

    std::unique_ptr<ScandProcess> m_process;

    std::unique_ptr<ScandEventFactory> m_eventFactory;
    std::unique_ptr<ScandActionFactory> m_actionFactory;

    std::unique_ptr<ScandRxRouter> m_rxRouter;
    std::unique_ptr<ScandTxRouter> m_txRouter;

    std::unique_ptr<ScandServiceManager> m_serviceManager;
};

}
