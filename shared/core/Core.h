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

protected:
    virtual bool onInit() = 0;
    virtual void onLoop() = 0;
    virtual void onShutdown() = 0;

    // Called when a SIGHUP reload request has been picked up from the main
    // loop. Default implementation drops the cached running-config so the
    // next tuningSection()/daemonConfig() lookup re-reads it from disk.
    // Daemons needing extra reload work (re-arming timers, etc.) can override.
    virtual void onReload();

    bool stopping() const;

    // Polls and clears the SIGHUP-set reload flag, invoking onReload() when
    // set. Daemons should call this once per onLoop() iteration alongside
    // stopping(), e.g. `while (!stopping()) { checkReload(); ... }`.
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
    static std::atomic<bool> m_reloadRequested;
};

} // namespace pz::core
