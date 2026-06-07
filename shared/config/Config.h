#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace pz::config
{

class Config
{
public:
    bool loadStartupConfig(const std::string& daemonName);
    const nlohmann::json& json() const;

    // Running-config: JSON state determined at runtime (e.g. aggregated heartbeat
    // results), persisted separately from the boot-time startup-config.
    static bool saveRunningConfig(const std::string& daemonName, const nlohmann::json& json);
    static bool loadRunningConfig(const std::string& daemonName, nlohmann::json& outJson);

private:
    nlohmann::json m_json;

};

}
