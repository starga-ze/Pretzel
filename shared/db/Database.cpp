#include "db/Database.h"

#include <iostream>
#include <libpq-fe.h>

namespace pz::db
{

namespace
{

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
CREATE TABLE IF NOT EXISTS state_snapshot (
    daemon      TEXT PRIMARY KEY,
    snapshot    JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
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
-- Unified per-IP device inventory for the probe pipeline (ICMP reachability -> SNMP
-- -> vendor API). One row per IP; each column is owned by the stage that writes it.
-- Replaces the former split icmp_devices / snmp_devices tables. Written exclusively by
-- engined (the single DB writer): ProbeService owns the ICMP columns + row lifecycle,
-- ScanService owns the SNMP/API columns. mgmtd reads it for /api/devices.
CREATE TABLE IF NOT EXISTS probe_devices (
    ip               TEXT PRIMARY KEY,
    -- ICMP stage: reachability + ARP-learned MAC and its OUI vendor.
    status           TEXT,
    hostname         TEXT,
    mac              TEXT,
    host_vendor      TEXT,
    -- SNMP / vendor-API stage: identity + topology.
    sys_name         TEXT,
    sys_descr        TEXT,
    sys_object_id    TEXT,
    sys_contact      TEXT,
    sys_location     TEXT,
    sys_uptime_ticks BIGINT,
    interface_macs   JSONB,
    interfaces       JSONB,
    if_table         JSONB,
    lldp_neighbors   JSONB,
    arp_entries      JSONB,
    snmp_vendor      TEXT,
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- One-time migration from the legacy split tables. Runs only while they still exist;
-- folds each into probe_devices (column ownership preserved), then drops them, so
-- later boots skip it entirely. This DDL is run only by engined (Config::preflight,
-- the single-writer path); the advisory lock is a cheap defensive guard. Legacy
-- `devices` (pre-rename) is handled too.
DO $migrate$
BEGIN
    PERFORM pg_advisory_xact_lock(906151321);
    IF to_regclass('public.devices') IS NOT NULL THEN
        ALTER TABLE devices RENAME TO icmp_devices;
    END IF;
    IF to_regclass('public.icmp_devices') IS NOT NULL THEN
        INSERT INTO probe_devices (ip, status, hostname, mac, host_vendor)
            SELECT ip, status, hostname, mac, vendor FROM icmp_devices
            ON CONFLICT (ip) DO UPDATE SET
                status = EXCLUDED.status, hostname = EXCLUDED.hostname,
                mac = EXCLUDED.mac, host_vendor = EXCLUDED.host_vendor;
        DROP TABLE icmp_devices;
    END IF;
    IF to_regclass('public.snmp_devices') IS NOT NULL THEN
        INSERT INTO probe_devices (ip, sys_name, sys_descr, sys_object_id, sys_contact,
                                   sys_location, sys_uptime_ticks, interface_macs,
                                   interfaces, if_table, lldp_neighbors, arp_entries,
                                   snmp_vendor)
            SELECT ip, sys_name, sys_descr, sys_object_id, sys_contact, sys_location,
                   sys_uptime_ticks, interface_macs, interfaces, if_table,
                   lldp_neighbors, arp_entries, vendor FROM snmp_devices
            ON CONFLICT (ip) DO UPDATE SET
                sys_name = EXCLUDED.sys_name, sys_descr = EXCLUDED.sys_descr,
                sys_object_id = EXCLUDED.sys_object_id, sys_contact = EXCLUDED.sys_contact,
                sys_location = EXCLUDED.sys_location,
                sys_uptime_ticks = EXCLUDED.sys_uptime_ticks,
                interface_macs = EXCLUDED.interface_macs, interfaces = EXCLUDED.interfaces,
                if_table = EXCLUDED.if_table, lldp_neighbors = EXCLUDED.lldp_neighbors,
                arp_entries = EXCLUDED.arp_entries, snmp_vendor = EXCLUDED.snmp_vendor;
        DROP TABLE snmp_devices;
    END IF;
    -- Daemon rename (snmpd -> scand): move the top-level config section so the renamed
    -- daemon finds its settings (incl. v3 / vendor-API credentials) across every
    -- running_config version and the startup_config baseline. Idempotent — once moved,
    -- the `? 'snmpd'` guard is false.
    UPDATE running_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('scand', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    UPDATE startup_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('scand', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    -- Drop the dead ipcd.service.daemon_map: routing uses the compiled IpcDaemon enum,
    -- never this config key, and its numbering had drifted from the enum. Strip the
    -- stale nested key from every persisted version. Idempotent — once removed, the
    -- `? 'daemon_map'` guard is false.
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
