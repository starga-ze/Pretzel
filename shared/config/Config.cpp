#include "Config.h"

#include "db/Database.h"
#include "util/PasswordHash.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <thread>

namespace pz::config
{

namespace
{

constexpr const char* kDefaultAdminUser = "admin";
constexpr const char* kDefaultAdminPassword = "admin";

std::string envOr(const char* envVar, const std::string& fallback)
{
    const char* value = std::getenv(envVar);
    return (value && *value) ? std::string(value) : fallback;
}

std::string configDir()
{
    return envOr("PRETZEL_CONFIG_DIR", "/etc/pretzel");
}

std::string startupConfigPath()
{
    return configDir() + "/startup-config.json";
}

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

bool bootstrapDatabase()
{
    static bool s_ready = false;

    if (s_ready)
        return true;

    pz::db::ConnParams params;
    try
    {
        const nlohmann::json root = readStartupFile();
        const nlohmann::json db = root.value("mgmtd", nlohmann::json::object())
                                      .value("service", nlohmann::json::object())
                                      .value("database", nlohmann::json::object());
        if (db.is_object())
        {
            params.host = db.value("host", params.host);
            params.name = db.value("name", params.name);
            params.user = db.value("user", params.user);
            params.password = db.value("password", params.password);
            if (db.contains("port"))
            {
                const auto& p = db["port"];
                params.port = p.is_string() ? p.get<std::string>() : std::to_string(p.get<long long>());
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "config: database block parse failed (" << e.what() << "), using defaults" << std::endl;
    }

    if (!pz::db::Database::instance().connect(params))
    {
        std::cerr << "config: database unavailable (host=" << params.host << " port=" << params.port
                  << " db=" << params.name << ")" << std::endl;
        return false;
    }

    s_ready = true;
    return true;
}

std::optional<std::pair<std::uint64_t, nlohmann::json>> latestRunningConfig()
{
    const auto rows = pz::db::Database::instance().queryRows("SELECT version, config_json FROM running_config "
                                                             "WHERE state = 'active' ORDER BY version DESC LIMIT 1");
    if (rows.empty() || rows.front().size() < 2)
        return std::nullopt;

    std::uint64_t version = 0;
    try
    {
        version = std::stoull(rows.front()[0]);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }

    auto parsed = nlohmann::json::parse(rows.front()[1], nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        return std::nullopt;
    return std::make_pair(version, std::move(parsed));
}

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

    if (auto d = s->find("database"); d != s->end() && d->is_object())
        d->erase("password");

    if (auto h = s->find("http"); h != s->end() && h->is_object())
        h->erase("admin");

    return root;
}

std::uint64_t& runningConfigVersionCache()
{
    static std::uint64_t s_version = 0;
    return s_version;
}

bool& seederProcess()
{
    static bool s_seeder = false;
    return s_seeder;
}

constexpr int kSeedWaitMaxAttempts = 30;
constexpr int kSeedWaitDelayMs = 500;

nlohmann::json loadRunningConfigRoot()
{
    runningConfigVersionCache() = 0;

    if (!bootstrapDatabase())
        return readStartupFile();

    if (auto latest = latestRunningConfig())
    {
        runningConfigVersionCache() = latest->first;
        return std::move(latest->second);
    }

    if (!seederProcess())
    {
        for (int attempt = 1; attempt <= kSeedWaitMaxAttempts; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kSeedWaitDelayMs));

            if (auto latest = latestRunningConfig())
            {
                runningConfigVersionCache() = latest->first;
                std::cerr << "config: running_config appeared after " << attempt * kSeedWaitDelayMs
                          << "ms (version=" << latest->first << ")" << std::endl;
                return std::move(latest->second);
            }
        }
        std::cerr << "config: running_config still empty after " << kSeedWaitMaxAttempts * kSeedWaitDelayMs
                  << "ms — falling back to startup-config (version 0, degraded)" << std::endl;
    }

    return readStartupFile();
}

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
        runningConfigCache() = loadRunningConfigRoot();
        runningConfigLoaded() = true;
    }
    return runningConfigCache();
}

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

const nlohmann::json& nestedSection(const std::string& daemonName, const char* parent, const std::string& domain)
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

}

bool Config::load(const std::string& daemonName)
{
    m_json = effectiveDaemon(daemonName);
    if (m_json.empty())
    {
        std::cerr << "config: no effective config for daemon '" << daemonName
                  << "' (startup-config missing and DB empty?) — using defaults" << std::endl;
    }
    return true;
}

const nlohmann::json& Config::json() const
{
    return m_json;
}

const nlohmann::json& Config::serviceSection(const std::string& daemonName, const std::string& domain)
{
    return nestedSection(daemonName, "service", domain);
}

const nlohmann::json& Config::systemSection(const std::string& daemonName, const std::string& domain)
{
    return nestedSection(daemonName, "system", domain);
}

const nlohmann::json& Config::daemonConfig(const std::string& daemonName)
{
    return effectiveDaemon(daemonName);
}

nlohmann::json Config::runningConfigRoot()
{
    return cachedRunningConfig();
}

bool Config::preflight()
{
    seederProcess() = true;

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
        std::cerr << "seedStore: startup-config empty/unreadable: " << startupConfigPath() << std::endl;
        return false;
    }

    auto& dbi = pz::db::Database::instance();

    const std::string persist = redactSecretsForPersist(startup).dump();

    if (!dbi.exec("INSERT INTO startup_config (id, config_json) VALUES (1, $1::jsonb) "
                  "ON CONFLICT (id) DO UPDATE SET config_json = EXCLUDED.config_json, "
                  "updated_at = now()",
                  {persist}))
    {
        std::cerr << "seedStore: startup_config upsert failed" << std::endl;
    }

    if (!dbi.exec("INSERT INTO running_config (version, config_json) "
                  "SELECT 1, $1::jsonb WHERE NOT EXISTS (SELECT 1 FROM running_config)",
                  {persist}))
    {
        std::cerr << "seedStore: running_config v1 seed failed" << std::endl;
        return false;
    }

    {
        const std::string salt = pz::util::generateSalt();
        const std::string hash = pz::util::hashSha256(kDefaultAdminPassword, salt);
        if (!dbi.exec("INSERT INTO local_users (username, password_hash, salt, must_change) "
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

    auto& db = pz::db::Database::instance();

    if (!db.exec("BEGIN"))
    {
        std::cerr << "commitConfig: BEGIN failed" << std::endl;
        return false;
    }

    const bool ok = db.exec("UPDATE running_config SET state = 'superseded' WHERE state = 'active'") &&
                    db.exec("INSERT INTO running_config (version, config_json, state) "
                            "VALUES ((SELECT COALESCE(MAX(version), 0) + 1 FROM running_config), "
                            "$1::jsonb, 'active')",
                            {redactSecretsForPersist(fullRoot).dump()});

    if (!ok || !db.exec("COMMIT"))
    {
        db.exec("ROLLBACK");
        std::cerr << "commitConfig: transaction failed, rolled back" << std::endl;
        return false;
    }

    invalidateConfigCache();
    return true;
}

bool Config::saveStateSnapshot(const std::string& daemonName, const nlohmann::json& json)
{
    if (!bootstrapDatabase())
    {
        std::cerr << "saveStateSnapshot: database unavailable for daemon '" << daemonName << "'" << std::endl;
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

    const auto snapshot =
        pz::db::Database::instance().queryScalar("SELECT snapshot FROM state_snapshot WHERE daemon = $1", {daemonName});
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

std::uint64_t Config::runningConfigVersion()
{
    cachedRunningConfig();
    return runningConfigVersionCache();
}

}
