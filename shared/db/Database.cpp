#include "db/Database.h"

#include <iostream>
#include <libpq-fe.h>

namespace pz::db
{

namespace
{

constexpr const char* kSchemaDDL = R"SQL(
CREATE TABLE IF NOT EXISTS startup_config (
    oid         INT PRIMARY KEY DEFAULT 1 CHECK (oid = 1),
    config_json JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS running_config (
    oid          BIGSERIAL   PRIMARY KEY,
    version      BIGINT      NOT NULL UNIQUE,
    config_json  JSONB       NOT NULL,
    committed_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    state        TEXT        NOT NULL DEFAULT 'active'
        CONSTRAINT running_config_state_check CHECK (state IN ('pending','active','superseded'))
);
-- Upgrade path for databases created before running_config.state existed (config-
-- version convergence: 'pending' on commit, 'active' once the fleet has converged).
ALTER TABLE running_config ADD COLUMN IF NOT EXISTS state TEXT NOT NULL DEFAULT 'active';
DO $rc_state$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'running_config_state_check') THEN
        ALTER TABLE running_config
            ADD CONSTRAINT running_config_state_check CHECK (state IN ('pending','active','superseded'));
    END IF;
END $rc_state$;
-- Identity-column naming: every configuration object has exactly ONE identity, `oid` — a UUID
-- string issued at creation. Rename pre-existing `id` columns on the persistent tables
-- (projections are drop+recreated).
DO $rename_oid$
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_name = 'startup_config' AND column_name = 'id') THEN
        ALTER TABLE startup_config RENAME COLUMN id TO oid;
    END IF;
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_name = 'running_config' AND column_name = 'id') THEN
        ALTER TABLE running_config RENAME COLUMN id TO oid;
    END IF;
END $rename_oid$;
-- Single-identity merge: objects used to carry `uuid` plus a separate numeric `oid`. Fold uuid
-- into oid and drop the numeric one across every persisted version and the baseline.
DO $merge_oid$
DECLARE
    tbl  TEXT;
    spec TEXT;
    path TEXT[];
BEGIN
    FOREACH tbl IN ARRAY ARRAY['running_config', 'startup_config'] LOOP
        FOREACH spec IN ARRAY ARRAY['icmpd.service.probe.probe_targets',
                                    'scand.service.api.auth_profiles',
                                    'scand.service.api.connectors',
                                    'engined.service.site.sites'] LOOP
            path := string_to_array(spec, '.');
            EXECUTE format($fmt$
                UPDATE %I SET config_json = jsonb_set(config_json, %L, (
                    SELECT COALESCE(jsonb_agg(
                        CASE WHEN elem ? 'uuid'
                             THEN (elem - 'uuid') || jsonb_build_object('oid', elem->'uuid')
                             ELSE elem END), '[]'::jsonb)
                    FROM jsonb_array_elements(config_json #> %L) AS elem))
                WHERE jsonb_typeof(config_json #> %L) = 'array'
                  AND EXISTS (SELECT 1 FROM jsonb_array_elements(config_json #> %L) AS e
                              WHERE e ? 'uuid')
            $fmt$, tbl, path, path, path, path);
        END LOOP;
    END LOOP;
END $merge_oid$;
-- Local login accounts (operator credentials), stored hashed. A non-versioned store
-- (NOT running_config) so password changes don't pollute the config version history.
-- Keyed by username so it extends to multiple local users / a future CLI daemon.
DROP TABLE IF EXISTS admin_user;
CREATE TABLE IF NOT EXISTS local_users (
    username      TEXT PRIMARY KEY,
    password_hash TEXT NOT NULL,
    salt          TEXT NOT NULL,
    must_change   BOOLEAN NOT NULL DEFAULT true,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- Legacy tables removed: probe_devices (mixed ICMP status + discovered SNMP/interface/
-- LLDP data) and state_snapshot (heartbeat snapshot that was written but never read).
DROP TABLE IF EXISTS probe_devices;
DROP TABLE IF EXISTS state_snapshot;
-- device_credentials was an abandoned first pass at the encrypted credential store: no DDL, no
-- reader, no writer, and it survived every reset because nothing listed it. It held cipher text
-- nothing could decrypt, so it goes. api_key_state/api_endpoint_state were declared before they
-- had a writer; see the note further down.
DROP TABLE IF EXISTS device_credentials;
DROP TABLE IF EXISTS api_endpoint_state;
-- ── Config vs state ─────────────────────────────────────────────────────────────
-- running_config holds what the OPERATOR declared: sites, devices, API keys, endpoints,
-- connectors. It is append-versioned, diffed before publish and revertable, so only things a
-- human authored belong in it.
--
-- Everything the SYSTEM produces lives in the tables below instead — issued API keys, expiry,
-- probe status, test outcomes. Writing those into running_config would mint a new configuration
-- version every time a key was re-issued or a probe answered, and would show machine noise in
-- the operator's review diff. engined is the single writer for all of them.

-- Devices projected from running_config, plus live reachability. A pure projection (rebuilt
-- from config on every reload), so the DDL drops and recreates it to evolve the shape.
--   oid         : object identity — a UUID string, immutable
--   site        : oid of the site the device belongs to ('' = unassigned)
--   device_type : ngfw (reached at its own address) | prisma_access (tenant-scoped)
--   target      : access identifier — ngfw: mgmt IP/FQDN · prisma_access: tenant/TSG id
DROP TABLE IF EXISTS inventory;
DROP TABLE IF EXISTS devices;
CREATE TABLE IF NOT EXISTS devices (
    oid         TEXT PRIMARY KEY,
    site        TEXT,
    device_type TEXT NOT NULL DEFAULT 'ngfw',
    target      TEXT,
    name        TEXT,
    description TEXT,
    status      TEXT,
    last_seen   TIMESTAMPTZ,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- One device per access identifier, per device type (dup-prevention).
CREATE UNIQUE INDEX IF NOT EXISTS devices_target_uniq ON devices (device_type, target)
    WHERE target IS NOT NULL AND target <> '';

-- What pretzel learns about a device API key, as opposed to what the operator declared. The
-- declaration (name, device, endpoint, account) lives in running_config; the issued secret and
-- its verification history live here, because running_config is append-versioned, shown verbatim
-- in the review diff and exported by Save-to-file — a key written there would be permanent,
-- readable by every reviewer, and would mint a configuration version each time it was re-issued.
-- Same reasoning that keeps admin passwords in local_users.
--
-- Written only by engined (mgmtd hands the result over by IPC). Keyed by the API Key oid.
--   secret_enc : AES-256-GCM, base64(nonce ‖ tag ‖ ciphertext), sealed by mgmtd with
--                /etc/pretzel/credentials.key. A database copy without that file is useless.
--   expires_at : NULL means no expiry — PAN-OS keys are indefinite unless an API key lifetime
--                is configured on the device.
CREATE TABLE IF NOT EXISTS api_key_state (
    oid            TEXT PRIMARY KEY,
    secret_enc     TEXT,
    issued_at      TIMESTAMPTZ,
    expires_at     TIMESTAMPTZ,
    last_test_at   TIMESTAMPTZ,
    last_test_ok   BOOLEAN,
    last_test_note TEXT,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- One-time config-json normalizations (idempotent; run by engined via Config::preflight).
DO $migrate$
BEGIN
    -- Daemon rename (snmpd -> scand): move the top-level config section so the renamed
    -- daemon finds its settings across every running_config version and the startup_config
    -- baseline. Idempotent — once moved, the `? 'snmpd'` guard is false.
    UPDATE running_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('scand', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    UPDATE startup_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('scand', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    -- Drop the dead ipcd.service.daemon_map: routing uses the compiled IpcDaemon enum,
    -- never this config key. Strip the stale nested key from every persisted version.
    UPDATE running_config SET config_json = config_json #- '{ipcd,service,daemon_map}'
        WHERE config_json #> '{ipcd,service}' ? 'daemon_map';
    UPDATE startup_config SET config_json = config_json #- '{ipcd,service,daemon_map}'
        WHERE config_json #> '{ipcd,service}' ? 'daemon_map';
END $migrate$;
)SQL";

std::vector<const char*> toParamPtrs(const std::vector<std::string>& params)
{
    std::vector<const char*> ptrs;
    ptrs.reserve(params.size());
    for (const auto& p : params)
        ptrs.push_back(p.c_str());
    return ptrs;
}

}

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
    m_params = params;
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

        PQreset(m_conn);
        if (PQstatus(m_conn) == CONNECTION_OK)
            return true;

        PQfinish(m_conn);
        m_conn = nullptr;
    }

    const char* keywords[] = {"host", "port", "dbname", "user", "password", nullptr};
    const char* values[] = {m_params.host.c_str(), m_params.port.c_str(),     m_params.name.c_str(),
                            m_params.user.c_str(), m_params.password.c_str(), nullptr};

    m_conn = PQconnectdbParams(keywords, values, 0);

    if (PQstatus(m_conn) != CONNECTION_OK)
    {
        std::cerr << "db: connection failed: " << PQerrorMessage(m_conn);
        PQfinish(m_conn);
        m_conn = nullptr;
        return false;
    }

    PQsetNoticeProcessor(
        m_conn, [](void*, const char*) {}, nullptr);

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

    PGresult* res = PQexecParams(m_conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
                                 ptrs.empty() ? nullptr : ptrs.data(), nullptr, nullptr, 0);

    const ExecStatusType st = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
    const bool ok = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK);
    if (!ok)
        std::cerr << "db: exec failed: " << PQerrorMessage(m_conn);
    PQclear(res);
    return ok;
}

std::optional<std::string> Database::queryScalar(const std::string& sql, const std::vector<std::string>& params)
{
    if (!ensureLive())
        return std::nullopt;

    const auto ptrs = toParamPtrs(params);

    PGresult* res = PQexecParams(m_conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
                                 ptrs.empty() ? nullptr : ptrs.data(), nullptr, nullptr, 0);

    std::optional<std::string> out;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0 && PQnfields(res) > 0 &&
        !PQgetisnull(res, 0, 0))
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

std::vector<std::vector<std::string>> Database::queryRows(const std::string& sql,
                                                          const std::vector<std::string>& params)
{
    std::vector<std::vector<std::string>> rows;

    if (!ensureLive())
        return rows;

    const auto ptrs = toParamPtrs(params);

    PGresult* res = PQexecParams(m_conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
                                 ptrs.empty() ? nullptr : ptrs.data(), nullptr, nullptr, 0);

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

}
