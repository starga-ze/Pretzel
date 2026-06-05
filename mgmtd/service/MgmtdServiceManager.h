#pragma once

#include "service/auth/AuthService.h"
#include "service/metrics/MetricsService.h"

#include <chrono>

namespace nf::mgmtd
{

class MgmtdServiceManager
{
public:
    MgmtdServiceManager() = default;
    ~MgmtdServiceManager() = default;

    void start();
    void schedule();
    void execute();

    AuthService& authService();
    MetricsService& metricsService();

private:
    AuthService m_authService;
    MetricsService m_metricsService;
};

} // namespace nf::mgmtd
