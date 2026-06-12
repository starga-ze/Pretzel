#pragma once

#include "core/Core.h"

#include "config/ConfigTypes.h"
#include "http/HttpServer.h"
#include "ipc/IpcClient.h"
#include "process/MgmtdProcess.h"
#include "router/MgmtdRxRouter.h"
#include "router/MgmtdTxRouter.h"
#include "service/MgmtdServiceManager.h"
#include "event/MgmtdEventFactory.h"
#include "action/MgmtdActionFactory.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace pz::mgmtd
{

struct HttpConfig
{
    std::string listenAddress{"0.0.0.0"};
    std::uint16_t listenPort{9101};

    bool tlsEnabled{false};
    std::string certFile;
    std::string keyFile;
};

class MgmtdCore : public pz::core::Core
{
public:
    MgmtdCore();
    ~MgmtdCore() override = default;

protected:
    void onPreConfigLoad() override;
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    bool loadLoggerConfig();
    bool loadIpcConfig();
    bool loadHttpConfig();
    bool loadAuthConfig();

    // Self-heal for the boot-time DB race: if seedStore() failed in onPreConfigLoad
    // because PostgreSQL was not yet accepting connections, retry from the main loop
    // until it succeeds. Also recovers the empty-tables state after a runtime DB wipe
    // or restart. No-op once the store is confirmed seeded.
    void ensureStoreSeeded();

private:
    pz::config::LoggerConfig m_loggerConfig;
    pz::config::IpcConfig m_ipcConfig;
    HttpConfig m_httpConfig;

    std::unique_ptr<pz::ipc::IpcClient>  m_ipcClient;
    std::unique_ptr<MgmtdEventFactory>   m_eventFactory;
    std::unique_ptr<MgmtdActionFactory>  m_actionFactory;
    std::unique_ptr<MgmtdTxRouter>       m_txRouter;
    std::unique_ptr<MgmtdServiceManager> m_serviceManager;
    std::unique_ptr<MgmtdRxRouter>       m_rxRouter;
    std::unique_ptr<HttpServer>          m_httpServer;
    std::unique_ptr<MgmtdProcess>        m_process;

    // Config-store seed state. Set in onPreConfigLoad(); when false, ensureStoreSeeded()
    // keeps retrying (throttled by m_lastSeedAttempt) until the DB is reachable.
    bool m_storeSeeded{false};
    std::chrono::steady_clock::time_point m_lastSeedAttempt{};
};

} // namespace pz::mgmtd
