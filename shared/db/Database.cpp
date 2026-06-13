#include "db/Database.h"

#include <iostream>
#include <libpq-fe.h>

namespace pz::db
{

namespace
{

// DDL for the full pretzel schema. MUST stay in sync with shared/db/schema.sql
// (the standalone script). Two-table config model: startup_config (baseline boot
// config, singleton) and running_config (live, versioned history). state_snapshot
// and devices hold runtime DATA. Idempotent (IF NOT EXISTS) so every daemon can
// safely run it on every boot.
constexpr const char* kSchemaDDL = R"SQL(
CREATE TABLE IF NOT EXISTS startup_config (
    id          INT PRIMARY KEY DEFAULT 1 CHECK (id = 1),
    config_json JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS running_config (
    id           BIGSERIAL   PRIMARY KEY,
    version      BIGINT      NOT NULL UNIQUE,
    config_json  JSONB       NOT NULL,
    committed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS state_snapshot (
    daemon      TEXT PRIMARY KEY,
    snapshot    JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
ALTER TABLE IF EXISTS devices RENAME TO icmp_devices;
CREATE TABLE IF NOT EXISTS icmp_devices (
    ip               TEXT PRIMARY KEY,
    status           TEXT,
    hostname         TEXT,
    sys_name         TEXT,
    sys_descr        TEXT,
    sys_object_id    TEXT,
    sys_contact      TEXT,
    sys_location     TEXT,
    sys_uptime_ticks BIGINT,
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- The admin credential now lives in running_config (mgmtd.service.http.admin),
-- commercial-gear style. Drop the legacy table on existing databases.
DROP TABLE IF EXISTS admin_user;
CREATE TABLE IF NOT EXISTS snmp_devices (
    ip               TEXT PRIMARY KEY,
    sys_name         TEXT,
    sys_descr        TEXT,
    sys_object_id    TEXT,
    sys_contact      TEXT,
    sys_location     TEXT,
    sys_uptime_ticks BIGINT,
    interface_macs   JSONB,
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);
)SQL";

// Builds the const char* array PQexecParams expects from a string vector. The
// backing strings must outlive the returned pointers (callers keep `params` alive).
std::vector<const char*> toParamPtrs(const std::vector<std::string>& params)
{
    std::vector<const char*> ptrs;
    ptrs.reserve(params.size());
    for (const auto& p : params)
        ptrs.push_back(p.c_str());
    return ptrs;
}

} // namespace

Database& Database::instance()
{
    static Database s_instance;
    return s_instance;
}

Database::~Database()
{
    if (m_conn)
    {
        PQfinish(m_conn);
        m_conn = nullptr;
    }
}

bool Database::connect(const ConnParams& params)
{
    m_params     = params;
    m_haveParams = true;

    return ensureLive();
}

bool Database::isConnected()
{
    return m_conn && PQstatus(m_conn) == CONNECTION_OK;
}

bool Database::ensureLive()
{
    if (!m_haveParams)
        return false;

    if (m_conn)
    {
        if (PQstatus(m_conn) == CONNECTION_OK)
            return true;

        // Try a cheap recovery before tearing the socket down.
        PQreset(m_conn);
        if (PQstatus(m_conn) == CONNECTION_OK)
            return true;

        PQfinish(m_conn);
        m_conn = nullptr;
    }

    const char* keywords[] = {"host", "port", "dbname", "user", "password", nullptr};
    const char* values[]   = {m_params.host.c_str(),
                              m_params.port.c_str(),
                              m_params.name.c_str(),
                              m_params.user.c_str(),
                              m_params.password.c_str(),
                              nullptr};

    m_conn = PQconnectdbParams(keywords, values, /*expand_dbname=*/0);

    if (PQstatus(m_conn) != CONNECTION_OK)
    {
        std::cerr << "db: connection failed: " << PQerrorMessage(m_conn);
        PQfinish(m_conn);
        m_conn = nullptr;
        return false;
    }

    // Swallow libpq NOTICE messages (e.g. "relation already exists, skipping" from
    // the idempotent ensureSchema DDL) so they do not spam each daemon's journal.
    PQsetNoticeProcessor(m_conn, [](void*, const char*) {}, nullptr);

    return true;
}

bool Database::ensureSchema()
{
    if (!ensureLive())
        return false;

    PGresult* res = PQexec(m_conn, kSchemaDDL);
    const bool ok = res && PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok)
        std::cerr << "db: ensureSchema failed: " << PQerrorMessage(m_conn);
    PQclear(res);
    return ok;
}

bool Database::exec(const std::string& sql, const std::vector<std::string>& params)
{
    if (!ensureLive())
        return false;

    const auto ptrs = toParamPtrs(params);

    PGresult* res = PQexecParams(m_conn,
                                 sql.c_str(),
                                 static_cast<int>(params.size()),
                                 nullptr,
                                 ptrs.empty() ? nullptr : ptrs.data(),
                                 nullptr,
                                 nullptr,
                                 0);

    const ExecStatusType st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
    const bool ok = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK);
    if (!ok)
        std::cerr << "db: exec failed: " << PQerrorMessage(m_conn);
    PQclear(res);
    return ok;
}

std::optional<std::string> Database::queryScalar(const std::string&              sql,
                                                 const std::vector<std::string>& params)
{
    if (!ensureLive())
        return std::nullopt;

    const auto ptrs = toParamPtrs(params);

    PGresult* res = PQexecParams(m_conn,
                                 sql.c_str(),
                                 static_cast<int>(params.size()),
                                 nullptr,
                                 ptrs.empty() ? nullptr : ptrs.data(),
                                 nullptr,
                                 nullptr,
                                 0);

    std::optional<std::string> out;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK &&
        PQntuples(res) > 0 && PQnfields(res) > 0 && !PQgetisnull(res, 0, 0))
    {
        out = std::string(PQgetvalue(res, 0, 0));
    }
    else if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::cerr << "db: queryScalar failed: " << PQerrorMessage(m_conn);
    }
    PQclear(res);
    return out;
}

std::vector<std::vector<std::string>> Database::queryRows(const std::string&              sql,
                                                          const std::vector<std::string>& params)
{
    std::vector<std::vector<std::string>> rows;

    if (!ensureLive())
        return rows;

    const auto ptrs = toParamPtrs(params);

    PGresult* res = PQexecParams(m_conn,
                                 sql.c_str(),
                                 static_cast<int>(params.size()),
                                 nullptr,
                                 ptrs.empty() ? nullptr : ptrs.data(),
                                 nullptr,
                                 nullptr,
                                 0);

    if (res && PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        const int nRows = PQntuples(res);
        const int nCols = PQnfields(res);
        rows.reserve(nRows);
        for (int r = 0; r < nRows; ++r)
        {
            std::vector<std::string> row;
            row.reserve(nCols);
            for (int c = 0; c < nCols; ++c)
                row.emplace_back(PQgetisnull(res, r, c) ? "" : PQgetvalue(res, r, c));
            rows.push_back(std::move(row));
        }
    }
    else
    {
        std::cerr << "db: queryRows failed: " << PQerrorMessage(m_conn);
    }
    PQclear(res);
    return rows;
}

} // namespace pz::db
