#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace pz::config
{

class Config
{
public:
    bool load(const std::string& daemonName);
    const nlohmann::json& json() const;

    static const nlohmann::json& serviceSection(const std::string& daemonName, const std::string& domain);
    static const nlohmann::json& systemSection(const std::string& daemonName, const std::string& domain);

    static const nlohmann::json& daemonConfig(const std::string& daemonName);

    static nlohmann::json runningConfigRoot();

    static std::uint64_t runningConfigVersion();

    static bool preflight();

    static bool seedStore();

    static bool commitConfig(const nlohmann::json& fullRoot);

    static void invalidateConfigCache();

private:
    nlohmann::json m_json;
};

}
