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

    // Schedules a reload for the current process by sending SIGHUP to itself.
    // Safe to call from any thread (e.g. an IPC RxRouter). The main loop
    // picks it up on the next checkReload() call.
    static void scheduleReload();

protected:
    // Called once at startup, BEFORE the config is loaded from the DB. Default is a
    // no-op; mgmtd overrides it to seed the config store (sync startup-config into
    // the DB and seed running_config v1 on a factory-fresh device). Other daemons
    // are read-only consumers and do not override it.
    virtual void onPreConfigLoad() {}

    virtual bool onInit() = 0;
    virtual void onLoop() = 0;
    virtual void onShutdown() = 0;

    // Called when a SIGHUP reload request has been picked up from the main
    // loop. Default implementation invalidates the config cache, then
    // re-execs the current process via /proc/self/exe so the daemon
    // restarts cleanly with the new running-config.
    // Override in sub-classes that need teardown before the exec.
    virtual void onReload();

    bool stopping() const;

    // Polls and clears the reload flag, invoking onReload() (which re-execs
    // the process) when set. Daemons call this once per onLoop() iteration.
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

} // namespace pz::core
