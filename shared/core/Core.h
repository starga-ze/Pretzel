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

    bool stopping() const;

    pz::config::Config m_config;

private:
    static void signalHandler(int signum);
    void handleSignal();

    std::string m_name;

    static std::atomic<bool> m_running;
};

} // namespace pz::core
