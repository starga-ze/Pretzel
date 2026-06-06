#pragma once

#include "service/auth/AuthService.h"
#include "service/metrics/MetricService.h"

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
    MetricService& metricService();

private:
    AuthService m_authService;
    MetricService m_metricService;
};

} // namespace nf::mgmtd
