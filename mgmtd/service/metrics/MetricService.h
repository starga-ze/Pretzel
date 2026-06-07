#pragma once

#include "service/metrics/MetricRegistry.h"

#include <chrono>
#include <string>

namespace pz::mgmtd
{

class MetricService
{
public:
    MetricService();

    void start();
    void tick(std::chrono::steady_clock::time_point now);

    std::string renderPrometheus() const;

private:
    MetricRegistry m_registry;
    std::chrono::steady_clock::time_point m_startedAt;
    std::chrono::steady_clock::time_point m_lastTickAt;
};

} // namespace pz::mgmtd
