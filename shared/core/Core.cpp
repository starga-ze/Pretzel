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

    std::cout << "SIGHUP received, reloading config (daemon={})" << m_name << std::endl;
    pz::config::Config::invalidateConfigCache();
    onReload();
}

void Core::onReload()
{
    std::cout << "config reload, restarting process (daemon={})" << m_name << std::endl;

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

    if (DIR* dir = ::opendir("/proc/self/fd"))
    {
        const int dfd = ::dirfd(dir);
        struct dirent* ent;
        while ((ent = ::readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.')
                continue;
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

    LOG_ERROR("execv failed (daemon={}, error={})", m_name, std::strerror(errno));
}

void Core::writePidFile()
{
    m_pidFilePath = "/run/pretzel/" + m_name + ".pid";

    FILE* f = std::fopen(m_pidFilePath.c_str(), "w");
    if (!f)
    {
        std::cerr << m_name << ": failed to write pid file " << m_pidFilePath << ": " << std::strerror(errno)
                  << std::endl;
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

}
