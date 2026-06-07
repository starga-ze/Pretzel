#include "Core.h"
#include "util/Logger.h"

#include <csignal>
#include <cstdio>
#include <iostream>
#include <unistd.h>

namespace pz::core
{

std::atomic<bool> Core::m_running{true};
std::atomic<bool> Core::m_reloadRequested{false};

Core::Core(std::string name) : m_name(std::move(name))
{
}

void Core::run()
{
    handleSignal();

    if (!m_config.load(m_name))
    {
        std::cerr << "running-config load failed for " << m_name << std::endl;
        return;
    }

    writePidFile();

    if (!onInit())
    {
        removePidFile();
        return;
    }

    onLoop();

    onShutdown();

    removePidFile();
}

void Core::handleSignal()
{
    std::signal(SIGINT, Core::signalHandler);
    std::signal(SIGTERM, Core::signalHandler);
    std::signal(SIGHUP, Core::signalHandler);
}

void Core::signalHandler(int signum)
{
    if (signum == SIGHUP)
    {
        m_reloadRequested = true;
        return;
    }
    m_running = false;
}

bool Core::stopping() const
{
    return !m_running.load();
}

void Core::checkReload()
{
    if (!m_reloadRequested.exchange(false))
    {
        return;
    }

    LOG_INFO("{}: SIGHUP received, reloading config", m_name);
    pz::config::Config::invalidateConfigCache();
    onReload();
}

void Core::onReload()
{
}

void Core::writePidFile()
{
    m_pidFilePath = "/run/pretzel/" + m_name + ".pid";

    FILE* f = std::fopen(m_pidFilePath.c_str(), "w");
    if (!f)
    {
        LOG_WARN("{}: failed to write pid file {}", m_name, m_pidFilePath);
        m_pidFilePath.clear();
        return;
    }
    std::fprintf(f, "%d\n", static_cast<int>(getpid()));
    std::fclose(f);
}

void Core::removePidFile()
{
    if (m_pidFilePath.empty())
    {
        return;
    }
    std::remove(m_pidFilePath.c_str());
    m_pidFilePath.clear();
}

} // namespace pz::core
