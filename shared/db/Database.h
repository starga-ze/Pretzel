#pragma once

#include <optional>
#include <string>
#include <vector>

// Forward-declare the libpq connection so this header does not leak <libpq-fe.h>
// into every translation unit that merely needs to issue a query.
struct pg_conn;
typedef struct pg_conn PGconn;

namespace pz::db
{

// Connection parameters for the pretzel PostgreSQL database. These come from the
// bootstrap source (the "mgmtd.service.database" block in the startup-config file)
// — they are the ONE thing that cannot live in the DB, since they are needed to
// reach it.
struct ConnParams
{
    std::string host     = "127.0.0.1";
    std::string port     = "5432";
    std::string name     = "pretzel";
    std::string user     = "pretzel";
    std::string password = "pretzel";
};

// Thin, process-wide libpq wrapper. A single connection is sufficient here: the
// daemons are single-threaded (cooperative polling), and every caller (config load
// at boot, heartbeat snapshot saves, device upserts, settings commits) is
// low-frequency. Methods transparently reconnect if the backend has dropped.
//
// The wrapper deliberately fails soft: when the database is unreachable, exec()
// returns false and the query helpers return std::nullopt / empty, letting the
// caller (pz::config::Config) fall back to its bundled file. This keeps the
// daemons bootable even if PostgreSQL is briefly down.
class Database
{
public:
    static Database& instance();

    // Records the connection params and opens the connection. Idempotent: calling
    // it again with the same params is a no-op while the connection is live.
    bool connect(const ConnParams& params);

    bool isConnected();

    // Creates the pretzel schema if it does not already exist (idempotent).
    // Safe to call from every daemon on every boot.
    bool ensureSchema();

    // Runs a statement that returns no rows (INSERT/UPDATE/DELETE/DDL).
    // `params` are bound as text via PQexecParams (injection-safe). Returns true
    // on success.
    bool exec(const std::string& sql, const std::vector<std::string>& params = {});

    // Returns the first column of the first row as text, or std::nullopt when the
    // query errors or yields no rows.
    std::optional<std::string> queryScalar(const std::string&              sql,
                                            const std::vector<std::string>& params = {});

    // Returns every row as a vector of text columns (empty on error/no rows).
    std::vector<std::vector<std::string>> queryRows(const std::string&              sql,
                                                    const std::vector<std::string>& params = {});

private:
    Database() = default;
    ~Database();
    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    // Reconnects if the socket has dropped.
    bool ensureLive();

    PGconn*     m_conn{nullptr};
    ConnParams  m_params;
    bool        m_haveParams{false};
};

} // namespace pz::db
