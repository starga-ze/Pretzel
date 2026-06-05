#pragma once

#include "service/metrics/MetricRegistry.h"

#include <chrono>
#include <string>

namespace nf::mgmtd
{

class MetricsService
{
public:
    MetricsService();

    void start();
    void tick(std::chrono::steady_clock::time_point now);

    std::string renderPrometheus() const;

private:
    MetricRegistry m_registry;
    std::chrono::steady_clock::time_point m_startedAt;
    std::chrono::steady_clock::time_point m_lastTickAt;
};

} // namespace nf::mgmtd
