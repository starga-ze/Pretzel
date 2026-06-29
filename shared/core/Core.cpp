#include "Core.h"
#include "util/Logger.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <vector>

namespace pz::core
{

std::atomic<bool> Core::m_running{true};
std::atomic<bool> Core::m_reload{false};

Core::Core(std::string name) : m_name(std::move(name))
{
}

void Core::run()
{
    handleSignal();

    // mgmtd seeds the config store here (sync startup-config + seed running_config
    // v1 if empty) before anyone reads it; other daemons no-op.
    onPreConfigLoad();

    if (!m_config.load(m_name))
    {
        std::cerr << "config load failed for " << m_name << std::endl;
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
        m_reload = true;
        return;
    }
    m_running = false;
}

bool Core::stopping() const
{
    return !m_running.load();
}

void Core::scheduleReload()
{
    ::kill(::getpid(), SIGHUP);
}

void Core::checkReload()
{
    if (!m_reload.exchange(false))
    {
        return;
    }

    LOG_INFO("SIGHUP received, reloading config (daemon={})", m_name);
    pz::config::Config::invalidateConfigCache();
    onReload();
}

void Core::onReload()
{
    LOG_INFO("config reload, restarting process (daemon={})", m_name);

    // Reconstruct original argv from /proc/self/cmdline (null-separated tokens).
    std::vector<std::string> argStrings;
    {
        std::ifstream f("/proc/self/cmdline");
        std::string token;
        while (std::getline(f, token, '\0'))
        {
            if (!token.empty())
                argStrings.push_back(std::move(token));
        }
    }

    if (argStrings.empty())
    {
        LOG_ERROR("restart aborted — could not read /proc/self/cmdline (daemon={})", m_name);
        return;
    }

    std::vector<char*> argv;
    argv.reserve(argStrings.size() + 1);
    for (auto& s : argStrings)
        argv.push_back(s.data());
    argv.push_back(nullptr);

    removePidFile();

    // Close all fds above stderr before exec. execv inherits open fds by
    // default; if the IPC socket stays open, ipcd never sees the disconnect
    // and blocks the reconnect attempt as a "route hijack".
    if (DIR* dir = ::opendir("/proc/self/fd"))
    {
        const int dfd = ::dirfd(dir);
        struct dirent* ent;
        while ((ent = ::readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.') continue;
            const int fd = std::atoi(ent->d_name);
            if (fd > 2 && fd != dfd)
                ::close(fd);
        }
        ::closedir(dir);
    }
    else
    {
        for (int fd = 3; fd < 1024; ++fd)
            ::close(fd);
    }

    ::execv("/proc/self/exe", argv.data());

    // execv only returns on failure.
    LOG_ERROR("execv failed (daemon={}, error={})", m_name, std::strerror(errno));
}

void Core::writePidFile()
{
    m_pidFilePath = "/run/pretzel/" + m_name + ".pid";

    FILE* f = std::fopen(m_pidFilePath.c_str(), "w");
    if (!f)
    {
        // Runs before Logger::Init() (in onInit), so spdlog isn't ready yet —
        // use stderr, consistent with onPreConfigLoad.
        std::cerr << m_name << ": failed to write pid file " << m_pidFilePath
                  << ": " << std::strerror(errno) << std::endl;
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
