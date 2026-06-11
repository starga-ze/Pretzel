#include "service/metrics/MetricRegistry.h"

// BUG FIX: was '#include "util/Logger.cpp"' — including a .cpp causes
//          duplicate-symbol linker errors. Use the header instead.
#include "util/Logger.h"

#include <sstream>

namespace pz::mgmtd
{

void MetricRegistry::setGauge(const std::string& name, double value)
{
    m_gauges[name] = value;
}

void MetricRegistry::incCounter(const std::string& name, std::uint64_t delta)
{
    m_counters[name] += delta;
}

std::string MetricRegistry::renderPrometheus() const
{
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

} // namespace pz::mgmtd
