#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace pz::mgmtd
{

class MetricRegistry
{
public:
    void setGauge(const std::string& name, double value);
    void incCounter(const std::string& name, std::uint64_t delta = 1);

    std::string renderPrometheus() const;

private:
    std::map<std::string, double> m_gauges;
    std::map<std::string, std::uint64_t> m_counters;
};

}
