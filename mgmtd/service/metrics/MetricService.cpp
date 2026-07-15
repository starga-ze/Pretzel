#include "service/metrics/MetricService.h"

namespace pz::mgmtd
{

MetricService::MetricService() : m_startedAt(std::chrono::steady_clock::now()), m_lastTickAt(m_startedAt)
{
}

void MetricService::start()
{
    const auto now = std::chrono::steady_clock::now();
    m_startedAt = now;
    m_lastTickAt = now;

    m_registry.setGauge("pretzel_mgmtd_up", 1);
    m_registry.setGauge("pretzel_mgmtd_uptime_seconds", 0);
    m_registry.incCounter("pretzel_mgmtd_http_requests_total", 0);
}

void MetricService::tick(std::chrono::steady_clock::time_point now)
{
    m_lastTickAt = now;

    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_startedAt).count();
    m_registry.setGauge("pretzel_mgmtd_up", 1);
    m_registry.setGauge("pretzel_mgmtd_uptime_seconds", static_cast<double>(uptime));
}

std::string MetricService::renderPrometheus() const
{
    return m_registry.renderPrometheus();
}

}
