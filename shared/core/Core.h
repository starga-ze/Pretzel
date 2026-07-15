#pragma once

#include "config/Config.h"

#include <atomic>
#include <string>

namespace pz::core
{

class Core
{
public:
    explicit Core(std::string name);
    virtual ~Core() = default;

    void run();

    static void scheduleReload();

protected:
    virtual void onPreConfigLoad()
    {
    }

    virtual bool onInit() = 0;
    virtual void onLoop() = 0;
    virtual void onShutdown() = 0;

    virtual void onReload();

    bool stopping() const;

    void checkReload();

    pz::config::Config m_config;

private:
    static void signalHandler(int signum);
    void handleSignal();

    void writePidFile();
    void removePidFile();

    std::string m_name;
    std::string m_pidFilePath;

    static std::atomic<bool> m_running;
    static std::atomic<bool> m_reload;
};

}
