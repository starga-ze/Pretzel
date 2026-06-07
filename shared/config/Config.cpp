#include "Config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace pz::config
{

namespace
{

// Resolves a deployment-root directory from an environment variable, falling
// back to the standard FHS location. This lets the same binary run against a
// real install (/etc, /var/lib) or an alternate root (e.g. for testing/containers)
// without requiring a rebuild — unlike the old PROJECT_ROOT compile-time constant,
// which baked in the source-checkout path and broke once deployed elsewhere.
std::string envDirOr(const char* envVar, const std::string& fallback)
{
    const char* value = std::getenv(envVar);
    return (value && *value) ? std::string(value) : fallback;
}

std::string configDir()
{
    return envDirOr("PRETZEL_CONFIG_DIR", "/etc/pretzel");
}

std::string stateDir()
{
    return envDirOr("PRETZEL_STATE_DIR", "/var/lib/pretzel");
}

std::string stateSnapshotPath(const std::string& daemonName)
{
    return stateDir() + "/" + daemonName + "/" + daemonName + "-state-snapshot.json";
}

std::string runningConfigPath()
{
    return configDir() + "/running-config.json";
}

nlohmann::json readJsonFile(const std::string& path)
{
    nlohmann::json json = nlohmann::json::object();

    std::ifstream file(path);
    if (file.is_open())
    {
        file >> json;
    }

    return json;
}

// The single consolidated running-config file is cached as one unit, keyed by
// daemon name at the top level. `s_loaded` distinguishes "not yet read" from
// "read and turned out empty" so invalidate -> re-read works correctly.
nlohmann::json& runningConfigCache()
{
    static nlohmann::json s_root = nlohmann::json::object();
    return s_root;
}

bool& runningConfigLoaded()
{
    static bool s_loaded = false;
    return s_loaded;
}

const nlohmann::json& cachedRunningConfig()
{
    if (!runningConfigLoaded())
    {
        runningConfigCache()  = readJsonFile(runningConfigPath());
        runningConfigLoaded() = true;
    }
    return runningConfigCache();
}

const nlohmann::json& cachedDaemonSection(const std::string& daemonName)
{
    static const nlohmann::json kEmpty = nlohmann::json::object();

    const nlohmann::json& root = cachedRunningConfig();

    auto it = root.find(daemonName);
    if (it == root.end() || !it->is_object())
        return kEmpty;

    return *it;
}

} // namespace

bool Config::load(const std::string& daemonName)
{
    const nlohmann::json& section = cachedDaemonSection(daemonName);
    if (section.empty())
    {
        std::cerr << "running-config: no section for daemon '" << daemonName
                  << "' in " << runningConfigPath() << std::endl;
        return false;
    }

    m_json = section;
    return true;
}

const nlohmann::json& Config::json() const
{
    return m_json;
}

bool Config::saveStateSnapshot(const std::string& daemonName, const nlohmann::json& json)
{
    std::string path = stateSnapshotPath(daemonName);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream file(path, std::ios::trunc);

    if (!file.is_open())
    {
        std::cerr << "File open failed, state-snapshot path: " << path << std::endl;
        return false;
    }

    file << json.dump(4);

    return true;
}

bool Config::loadStateSnapshot(const std::string& daemonName, nlohmann::json& outJson)
{
    std::string path = stateSnapshotPath(daemonName);

    std::ifstream file(path);

    if (!file.is_open())
    {
        return false;
    }

    file >> outJson;

    return true;
}

const nlohmann::json& Config::tuningSection(const std::string& daemonName, const std::string& domain)
{
    static const nlohmann::json kEmpty = nlohmann::json::object();

    const nlohmann::json& root = cachedDaemonSection(daemonName);

    auto tuningIt = root.find("tuning");
    if (tuningIt == root.end() || !tuningIt->is_object())
        return kEmpty;

    auto domainIt = tuningIt->find(domain);
    if (domainIt == tuningIt->end() || !domainIt->is_object())
        return kEmpty;

    return *domainIt;
}

const nlohmann::json& Config::daemonConfig(const std::string& daemonName)
{
    return cachedDaemonSection(daemonName);
}

bool Config::updateTuning(const std::string& daemonName,
                          const std::string& domain,
                          const nlohmann::json& values)
{
    if (!values.is_object())
        return false;

    std::string path = runningConfigPath();

    nlohmann::json root = readJsonFile(path);
    if (!root.is_object())
        return false;

    nlohmann::json& section = root[daemonName];
    if (!section.is_object())
        section = nlohmann::json::object();

    nlohmann::json& tuning = section["tuning"];
    if (!tuning.is_object())
        tuning = nlohmann::json::object();

    nlohmann::json& domainSection = tuning[domain];
    if (!domainSection.is_object())
        domainSection = nlohmann::json::object();

    domainSection.merge_patch(values);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
    {
        std::cerr << "File open failed, running-config path: " << path << std::endl;
        return false;
    }

    file << root.dump(4);

    runningConfigCache()  = root;
    runningConfigLoaded() = true;

    return true;
}

void Config::invalidateConfigCache()
{
    runningConfigLoaded() = false;
}

}
