#include "Config.h"

#include "db/Database.h"
#include "util/PasswordHash.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>

namespace pz::config
{

namespace
{

// Factory-default admin account, seeded (hashed) on a fresh device. There is no
// plaintext backdoor; the operator changes the password via /api/change-password.
constexpr const char* kDefaultAdminUser     = "admin";
constexpr const char* kDefaultAdminPassword = "admin";

// Reads an environment variable, falling back to a default when unset/empty.
std::string envOr(const char* envVar, const std::string& fallback)
{
    const char* value = std::getenv(envVar);
    return (value && *value) ? std::string(value) : fallback;
}

std::string configDir()
{
    return envOr("PRETZEL_CONFIG_DIR", "/etc/pretzel");
}

// The deployed startup-config file. It is the baseline boot config: the source for
// the DB connection params, the seed for the DB on a factory-fresh device, and the
// offline fallback when the DB is unreachable. start.py copies it here from
// config/startup-config.json before any daemon starts.
std::string startupConfigPath()
{
    return configDir() + "/startup-config.json";
}

// Parses the startup-config file. Defensive: returns an empty object on any error
// (missing file, bad JSON) so callers degrade gracefully instead of crashing.
nlohmann::json readStartupFile()
{
    try
    {
        std::ifstream file(startupConfigPath());
        if (!file.is_open())
        {
            std::cerr << "config: cannot open startup-config: " << startupConfigPath() << std::endl;
            return nlohmann::json::object();
        }
        nlohmann::json json;
        file >> json;
        return json.is_object() ? json : nlohmann::json::object();
    }
    catch (const std::exception& e)
    {
        std::cerr << "config: failed to parse startup-config: " << e.what() << std::endl;
        return nlohmann::json::object();
    }
}

// ── Bootstrap: connect to PostgreSQL ──
// The connection params are the one thing that cannot live in the DB, so they come
// from the startup-config file's "mgmtd.service.database" block, falling back to the
// compiled localhost defaults in ConnParams. Latches true on first success.
//
// Connect ONLY — the schema DDL is intentionally NOT applied here. engined owns the
// schema (it is the single DB writer and boots first) via Config::preflight(); every
// other daemon only connects + reads and relies on engined having ensured it.
bool bootstrapDatabase()
{
    static bool s_ready = false;

    if (s_ready)
        return true;

    pz::db::ConnParams params;
    try
    {
        const nlohmann::json root = readStartupFile();
        const nlohmann::json db =
            root.value("mgmtd", nlohmann::json::object())
                .value("service", nlohmann::json::object())
                .value("database", nlohmann::json::object());
        if (db.is_object())
        {
            params.host     = db.value("host", params.host);
            params.name     = db.value("name", params.name);
            params.user     = db.value("user", params.user);
            params.password = db.value("password", params.password);
            if (db.contains("port"))
            {
                const auto& p = db["port"];
                params.port = p.is_string() ? p.get<std::string>()
                                            : std::to_string(p.get<long long>());
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "config: database block parse failed (" << e.what()
                  << "), using defaults" << std::endl;
    }

    if (!pz::db::Database::instance().connect(params))
    {
        std::cerr << "config: database unavailable (host=" << params.host
                  << " port=" << params.port << " db=" << params.name << ")" << std::endl;
        return false;
    }

    s_ready = true;
    return true;
}

// Queries the latest (highest-version) running-config blob, if any.
std::optional<nlohmann::json> latestRunningConfig()
{
    const auto raw = pz::db::Database::instance().queryScalar(
        "SELECT config_json FROM running_config ORDER BY version DESC LIMIT 1");
    if (!raw)
        return std::nullopt;

    auto parsed = nlohmann::json::parse(*raw, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        return std::nullopt;
    return parsed;
}

// Secrets must never be persisted into the config tables in cleartext:
//   * mgmtd.service.database.password — the DB connection password is read from the
//     on-disk startup-config by bootstrapDatabase(), never from these tables.
//   * mgmtd.service.http.admin       — login credentials live in the local_users
//     table, never in config; this strips any stray admin block from a config blob.
// SNMP v3 credentials are intentionally left in place: snmpd consumes them from the
// running-config at runtime, so they are operator-managed configuration.
nlohmann::json redactSecretsForPersist(nlohmann::json root)
{
    if (!root.is_object())
        return root;

    auto m = root.find("mgmtd");
    if (m == root.end() || !m->is_object())
        return root;

    auto s = m->find("service");
    if (s == m->end() || !s->is_object())
        return root;

    // The DB connection password is read from the startup-config file at boot, never
    // persisted into the config tables.
    if (auto d = s->find("database"); d != s->end() && d->is_object())
        d->erase("password");

    // Login credentials never belong in config (they live in local_users). Strip any
    // admin block so it can't leak into config history or the settings API.
    if (auto h = s->find("http"); h != s->end() && h->is_object())
        h->erase("admin");

    return root;
}

// Implements the load decision tree (see Config.h):
//   Case B: running_config has a latest version  -> use it.
//   Case A: empty table OR DB unreachable        -> use the startup-config file.
// Fully defensive: any failure falls back to the startup-config file.
nlohmann::json loadRunningConfigRoot()
{
    if (!bootstrapDatabase())
        return readStartupFile();  // DB down — last-resort fallback

    if (auto latest = latestRunningConfig())
        return *latest;            // Case B: adopt latest committed version

    return readStartupFile();      // Case A: empty DB (engined seeds it via preflight)
}

// ── In-process cache ──
// The running-config root and the per-daemon "effective" configs (global merged
// with the daemon section) are cached. invalidateConfigCache() clears both.
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

std::map<std::string, nlohmann::json>& effectiveCache()
{
    static std::map<std::string, nlohmann::json> s_cache;
    return s_cache;
}

const nlohmann::json& cachedRunningConfig()
{
    if (!runningConfigLoaded())
    {
        runningConfigCache()  = loadRunningConfigRoot();
        runningConfigLoaded() = true;
    }
    return runningConfigCache();
}

// Effective config for a daemon: the shared "global" defaults overlaid with the
// daemon's own section (merge_patch = RFC-7386 deep override, daemon wins).
const nlohmann::json& effectiveDaemon(const std::string& daemonName)
{
    auto& cache = effectiveCache();
    auto it = cache.find(daemonName);
    if (it != cache.end())
        return it->second;

    const nlohmann::json& root = cachedRunningConfig();

    nlohmann::json eff = root.value("global", nlohmann::json::object());
    if (!eff.is_object())
        eff = nlohmann::json::object();

    auto dit = root.find(daemonName);
    if (dit != root.end() && dit->is_object())
        eff.merge_patch(*dit);

    return cache.emplace(daemonName, std::move(eff)).first->second;
}

// Returns effective(daemon)[parent][domain], or an empty object if absent.
const nlohmann::json& nestedSection(const std::string& daemonName,
                                    const char*        parent,
                                    const std::string& domain)
{
    static const nlohmann::json kEmpty = nlohmann::json::object();

    const nlohmann::json& eff = effectiveDaemon(daemonName);

    auto pit = eff.find(parent);
    if (pit == eff.end() || !pit->is_object())
        return kEmpty;

    auto dit = pit->find(domain);
    if (dit == pit->end() || !dit->is_object())
        return kEmpty;

    return *dit;
}

} // namespace

bool Config::load(const std::string& daemonName)
{
    m_json = effectiveDaemon(daemonName);
    if (m_json.empty())
    {
        std::cerr << "config: no effective config for daemon '" << daemonName
                  << "' (startup-config missing and DB empty?) — using defaults"
                  << std::endl;
    }
    return true;
}

const nlohmann::json& Config::json() const
{
    return m_json;
}

const nlohmann::json& Config::serviceSection(const std::string& daemonName,
                                             const std::string& domain)
{
    return nestedSection(daemonName, "service", domain);
}

const nlohmann::json& Config::systemSection(const std::string& daemonName,
                                            const std::string& domain)
{
    return nestedSection(daemonName, "system", domain);
}

const nlohmann::json& Config::daemonConfig(const std::string& daemonName)
{
    return effectiveDaemon(daemonName);
}

nlohmann::json Config::runningConfigRoot()
{
    return cachedRunningConfig();  // copy of the latest committed root
}

bool Config::preflight()
{
    // engined-only: connect, create the schema (single-writer DDL), then seed the
    // config store. Returns false if the DB is unreachable so the caller can retry.
    if (!bootstrapDatabase())
    {
        std::cerr << "preflight: database unavailable" << std::endl;
        return false;
    }

    if (!pz::db::Database::instance().ensureSchema())
    {
        std::cerr << "preflight: ensureSchema failed" << std::endl;
        return false;
    }

    return seedStore();
}

bool Config::seedStore()
{
    if (!bootstrapDatabase())
    {
        std::cerr << "seedStore: database unavailable" << std::endl;
        return false;
    }

    const nlohmann::json startup = readStartupFile();
    if (startup.empty())
    {
        std::cerr << "seedStore: startup-config empty/unreadable: "
                  << startupConfigPath() << std::endl;
        return false;
    }

    auto& dbi = pz::db::Database::instance();

    // Secrets (DB password, admin credential) never go into the config tables.
    const std::string persist = redactSecretsForPersist(startup).dump();

    // Sync the baseline startup_config (singleton row).
    if (!dbi.exec(
            "INSERT INTO startup_config (id, config_json) VALUES (1, $1::jsonb) "
            "ON CONFLICT (id) DO UPDATE SET config_json = EXCLUDED.config_json, "
            "updated_at = now()",
            {persist}))
    {
        std::cerr << "seedStore: startup_config upsert failed" << std::endl;
    }

    // Seed running_config version 1 ONLY when the history is empty. Existing versions
    // are preserved so committed changes survive a reboot.
    if (!dbi.exec(
            "INSERT INTO running_config (version, config_json) "
            "SELECT 1, $1::jsonb WHERE NOT EXISTS (SELECT 1 FROM running_config)",
            {persist}))
    {
        std::cerr << "seedStore: running_config v1 seed failed" << std::endl;
        return false;
    }

    // Seed the default admin login into local_users on a factory-fresh device (hashed;
    // no plaintext backdoor). must_change=true forces a change off the default on first
    // login. ON CONFLICT DO NOTHING keeps an already-provisioned account intact.
    {
        const std::string salt = pz::util::generateSalt();
        const std::string hash = pz::util::hashSha256(kDefaultAdminPassword, salt);
        if (!dbi.exec(
                "INSERT INTO local_users (username, password_hash, salt, must_change) "
                "VALUES ($1, $2, $3, true) ON CONFLICT (username) DO NOTHING",
                {kDefaultAdminUser, hash, salt}))
        {
            std::cerr << "seedStore: local_users default seed failed" << std::endl;
        }
    }

    return true;
}

bool Config::commitConfig(const nlohmann::json& fullRoot)
{
    if (!fullRoot.is_object())
    {
        std::cerr << "commitConfig: root is not an object" << std::endl;
        return false;
    }

    if (!bootstrapDatabase())
    {
        std::cerr << "commitConfig: database unavailable" << std::endl;
        return false;
    }

    const bool ok = pz::db::Database::instance().exec(
        "INSERT INTO running_config (version, config_json) "
        "VALUES ((SELECT COALESCE(MAX(version), 0) + 1 FROM running_config), $1::jsonb)",
        {redactSecretsForPersist(fullRoot).dump()});

    if (ok)
        invalidateConfigCache();  // next read adopts the new version

    return ok;
}

bool Config::saveStateSnapshot(const std::string& daemonName, const nlohmann::json& json)
{
    if (!bootstrapDatabase())
    {
        std::cerr << "saveStateSnapshot: database unavailable for daemon '"
                  << daemonName << "'" << std::endl;
        return false;
    }

    return pz::db::Database::instance().exec(
        "INSERT INTO state_snapshot (daemon, snapshot) VALUES ($1, $2::jsonb) "
        "ON CONFLICT (daemon) DO UPDATE SET snapshot = EXCLUDED.snapshot, updated_at = now()",
        {daemonName, json.dump()});
}

bool Config::loadStateSnapshot(const std::string& daemonName, nlohmann::json& outJson)
{
    if (!bootstrapDatabase())
        return false;

    const auto snapshot = pz::db::Database::instance().queryScalar(
        "SELECT snapshot FROM state_snapshot WHERE daemon = $1", {daemonName});
    if (!snapshot)
        return false;

    auto parsed = nlohmann::json::parse(*snapshot, nullptr, false);
    if (parsed.is_discarded())
        return false;

    outJson = std::move(parsed);
    return true;
}

void Config::invalidateConfigCache()
{
    runningConfigLoaded() = false;
    effectiveCache().clear();
}

}
