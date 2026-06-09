#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace pz::config
{

// All daemons share a single canonical config file (config/running-config.json),
// keyed by daemon name at the top level. The file is read at boot and cached;
// after mgmtd persists a change it broadcasts IpcCmd::ConfigReload, which causes
// every daemon to restart and re-read the file. This also maps cleanly onto a
// future DB-backed config: swap the file I/O for DB reads/writes and keep the
// same caching/reload flow.
class Config
{
public:
    // Loads this daemon's section (running-config[daemonName]) into m_json.
    bool load(const std::string& daemonName);
    const nlohmann::json& json() const;

    // State snapshot: JSON state determined at runtime (e.g. aggregated heartbeat
    // results), persisted separately from the running-config. This is
    // monitoring/runtime DATA, not operational configuration.
    static bool saveStateSnapshot(const std::string& daemonName, const nlohmann::json& json);
    static bool loadStateSnapshot(const std::string& daemonName, nlohmann::json& outJson);

    // Tuning: user-adjustable operational constants (intervals, timeouts, retry
    // counts, batch sizes, ...) read from running-config[daemonName]."tuning"."<domain>".
    // Services call this with their domain name and fall back to their compiled-in
    // defaults via json::value(key, default) when the section or individual keys
    // are absent, so behavior is unchanged until the user edits the config.
    // Results are cached (the whole running-config file is cached as one unit).
    static const nlohmann::json& tuningSection(const std::string& daemonName, const std::string& domain);

    // Returns the cached running-config section for a single daemon (used by
    // mgmtd to aggregate all daemons' settings for the settings dashboard).
    static const nlohmann::json& daemonConfig(const std::string& daemonName);

    // Merges `values` into running-config[daemonName]."tuning"."<domain>" on disk
    // (read-modify-write of the single consolidated file) and updates the cache.
    // Used by mgmtd's commit handler; after all changes are written mgmtd
    // broadcasts IpcCmd::ConfigReload so every daemon restarts and re-reads.
    static bool updateTuning(const std::string& daemonName,
                             const std::string& domain,
                             const nlohmann::json& values);

    // Drops the cached running-config so the next tuningSection()/daemonConfig()
    // call re-reads it from disk. Called by Core::checkReload() before onReload().
    static void invalidateConfigCache();

private:
    nlohmann::json m_json;

};

}
