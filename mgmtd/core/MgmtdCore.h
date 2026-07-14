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
    // No onPreConfigLoad override: mgmtd is a read-only DB consumer. engined owns the
    // pre-flight (schema + config seed) and boots first.
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    bool loadLoggerConfig();
    bool loadIpcConfig();
    bool loadHttpConfig();
    bool loadAuthConfig();

    // Fail-closed credential load: until a real local_users row is read, the account
    // has an empty hash and all logins are refused. Retries (throttled) from the main
    // loop so logins become possible once engined has seeded the credential. No-op once
    // loaded.
    void ensureCredentialLoaded();

private:
    std::chrono::steady_clock::time_point m_lastCredAttempt{};

    pz::config::LoggerConfig m_loggerConfig;
    pz::config::IpcConfig m_ipcConfig;
    HttpConfig m_httpConfig;

    std::unique_ptr<pz::ipc::IpcClient>  m_ipcClient;
    std::unique_ptr<MgmtdEventFactory>   m_eventFactory;
    std::unique_ptr<MgmtdActionFactory>  m_actionFactory;
    std::unique_ptr<MgmtdTxRouter>       m_txRouter;
    std::unique_ptr<MgmtdServiceManager> m_serviceManager;
    std::unique_ptr<MgmtdRxRouter>       m_rxRouter;
    std::unique_ptr<pz::http::HttpServer> m_httpServer;
    std::unique_ptr<MgmtdProcess>        m_process;
};

} // namespace pz::mgmtd
