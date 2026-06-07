#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace pz::config
{

// All daemons share a single canonical config file (config/running-config.json),
// keyed by daemon name at the top level — there is no separate "startup-config"
// snapshot. The file is read at boot and re-read on SIGHUP (see pz::core::Core),
// so the persisted file is always what's "running". This also maps cleanly onto
// a future DB-backed config: swap the file I/O for DB reads/writes and keep the
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
    // (read-modify-write of the single consolidated file) and drops the cached
    // copy so the next access re-reads it. Used by mgmtd to persist settings
    // changes; the owning daemon must still be told to reload — mgmtd does this
    // by sending SIGHUP to the daemon's pid (read from /run/pretzel/<daemon>.pid).
    static bool updateTuning(const std::string& daemonName,
                             const std::string& domain,
                             const nlohmann::json& values);

    // Drops the cached running-config so the next tuningSection()/daemonConfig()
    // call re-reads it from disk. Called by pz::core::Core when the daemon
    // receives SIGHUP (see Core::checkReload()/onReload()).
    static void invalidateConfigCache();

private:
    nlohmann::json m_json;

};

}
