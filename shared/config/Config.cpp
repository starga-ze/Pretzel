#include "Config.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace pz::config
{

namespace
{

std::string runningConfigPath(const std::string& daemonName)
{
    return std::string(PROJECT_ROOT) + "/config/running/" + daemonName + "/" + daemonName + "-running-config.json";
}

} // namespace

bool Config::loadStartupConfig(const std::string& daemonName)
{
    std::string path =
        std::string(PROJECT_ROOT) + "/config/startup/" + daemonName + "/" + daemonName + "-startup-config.json";

    std::ifstream file(path);

    if (!file.is_open())
    {
        std::cerr << "File open failed, config path: " << path << std::endl;
        return false;
    }

    file >> m_json;

    return true;
}

const nlohmann::json& Config::json() const
{
    return m_json;
}

bool Config::saveRunningConfig(const std::string& daemonName, const nlohmann::json& json)
{
    std::string path = runningConfigPath(daemonName);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream file(path, std::ios::trunc);

    if (!file.is_open())
    {
        std::cerr << "File open failed, running-config path: " << path << std::endl;
        return false;
    }

    file << json.dump(4);

    return true;
}

bool Config::loadRunningConfig(const std::string& daemonName, nlohmann::json& outJson)
{
    std::string path = runningConfigPath(daemonName);

    std::ifstream file(path);

    if (!file.is_open())
    {
        return false;
    }

    file >> outJson;

    return true;
}

}
