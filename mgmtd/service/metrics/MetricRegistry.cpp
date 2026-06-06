#include "service/metrics/MetricRegistry.h"

#include "util/Logger.cpp"

#include <sstream>

namespace nf::mgmtd
{

void MetricRegistry::setGauge(const std::string& name, double value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gauges[name] = value;
}

void MetricRegistry::incCounter(const std::string& name, std::uint64_t delta)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_counters[name] += delta;
}

std::string MetricRegistry::renderPrometheus() const
{
    LOG_INFO("renderPrometheus");
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream out;

    for (const auto& [name, value] : m_gauges)
    {
        out << "# TYPE " << name << " gauge\n";
        out << name << ' ' << value << "\n";
    }

    for (const auto& [name, value] : m_counters)
    {
        out << "# TYPE " << name << " counter\n";
        out << name << ' ' << value << "\n";
    }

    return out.str();
}

} // namespace nf::mgmtd
