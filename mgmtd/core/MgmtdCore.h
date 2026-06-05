#pragma once

#include "core/Core.h"

#include "config/ConfigTypes.h"
#include "http/HttpServer.h"
#include "ipc/IpcClient.h"
#include "process/MgmtdProcess.h"
#include "service/MgmtdServiceManager.h"

#include <cstdint>
#include <memory>
#include <string>

namespace nf::mgmtd
{

struct HttpConfig
{
    std::string listenAddress {"0.0.0.0"};
    std::uint16_t listenPort {9101};

    bool tlsEnabled {false};
    std::string certFile;
    std::string keyFile;
};

class MgmtdCore : public nf::core::Core
{
public:
    MgmtdCore();
    ~MgmtdCore() override = default;

protected:
    bool onInit() override;
    void onLoop() override;
    void onShutdown() override;

private:
    bool loadLoggerConfig();
    bool loadIpcConfig();
    bool loadHttpConfig();
    bool loadAuthConfig();

private:
    nf::config::LoggerConfig m_loggerConfig;
    nf::config::IpcConfig m_ipcConfig;
    HttpConfig m_httpConfig;

    std::unique_ptr<nf::ipc::IpcClient> m_ipcClient;
    std::unique_ptr<MgmtdServiceManager> m_serviceManager;
    std::unique_ptr<HttpServer> m_httpServer;
    std::unique_ptr<MgmtdProcess> m_process;
};

} // namespace nf::mgmtd
