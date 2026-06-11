#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace pz::config
{

// Two-table, versioned configuration model (commercial network-gear style).
//
//   startup-config : the baseline boot config shipped on disk
//                    (/etc/pretzel/startup-config.json), synced into the
//                    startup_config DB table by mgmtd at boot.
//   running-config : the live, authoritative config, kept as a VERSION HISTORY in
//                    the running_config DB table. The highest version is active.
//
// Decision tree at load (loadRunningConfigRoot):
//   * running_config has rows  -> adopt the latest version (normal reboot).
//   * running_config is empty   -> adopt the startup-config file (factory-fresh),
//                                  and mgmtd seeds it as version 1 (seedStore()).
//   * DB unreachable            -> fall back to the startup-config file.
//
// The effective config for a daemon is merge_patch(root["global"], root[daemon]),
// i.e. per-daemon values override the shared "global" defaults. Sections are split
// into "system" (logger, ipc) and "service" (daemon-specific) — there is no
// "tuning" concept. The whole root is cached in-process; invalidateConfigCache()
// drops it so the next read re-queries the latest version.
//
// The ONE thing that cannot live in the DB — the DB connection params — is read
// from the startup-config file's "mgmtd.service.database" block (falling back to
// compiled localhost defaults). See Config.cpp / pz::db::Database.
class Config
{
public:
    // Loads this daemon's effective config (global merged with its own section)
    // into m_json. Never fatal: an absent section leaves m_json empty so every
    // reader falls back to its compiled default via json::value(key, default).
    bool load(const std::string& daemonName);
    const nlohmann::json& json() const;

    // State snapshot: JSON state determined at runtime (e.g. aggregated heartbeat
    // results), persisted separately in the state_snapshot table. This is
    // monitoring/runtime DATA, not operational configuration.
    static bool saveStateSnapshot(const std::string& daemonName, const nlohmann::json& json);
    static bool loadStateSnapshot(const std::string& daemonName, nlohmann::json& outJson);

    // Effective (global-merged) "service".<domain> / "system".<domain> sections for
    // a daemon. Callers fall back to compiled defaults via json::value(key, default)
    // when a section or key is absent. Results are cached.
    static const nlohmann::json& serviceSection(const std::string& daemonName, const std::string& domain);
    static const nlohmann::json& systemSection(const std::string& daemonName, const std::string& domain);

    // The whole effective config for a single daemon (used by mgmtd to aggregate
    // every daemon's settings for the dashboard).
    static const nlohmann::json& daemonConfig(const std::string& daemonName);

    // A copy of the whole active running-config root (global + all daemon sections),
    // i.e. the latest committed version. Used by the commit pipeline as the base for
    // the next version.
    static nlohmann::json runningConfigRoot();

    // mgmtd-only bootstrap: sync the startup-config file into the startup_config
    // table, and seed running_config version 1 from it IF the table is empty
    // (existing history is preserved across reboots). Call before load().
    static bool seedStore();

    // Appends a new running_config version (= MAX(version)+1) with the given full
    // config root. Used by the HTTP commit pipeline; invalidates the cache so a
    // subsequent reload adopts the new version.
    static bool commitConfig(const nlohmann::json& fullRoot);

    // Drops the cached running-config so the next read re-queries the latest
    // version. Called by Core::checkReload() before onReload().
    static void invalidateConfigCache();

private:
    nlohmann::json m_json;
};

}
