-- shared/db/schema.sql
--
-- Canonical DDL for the pretzel config/state store. This standalone script is
-- mirrored by kSchemaDDL in shared/db/Database.cpp (keep the two in sync). Every
-- statement is idempotent (IF NOT EXISTS), so it is safe to apply on every boot.
--
-- Two-table configuration model (commercial network-gear style):
--   startup_config : baseline boot config (the shipped startup-config file).
--   running_config : live, versioned config history (the active running-config).

-- Baseline boot config. Synced from /etc/pretzel/startup-config.json by mgmtd at
-- boot. Singleton row (id = 1).
CREATE TABLE IF NOT EXISTS startup_config (
    id          INT PRIMARY KEY DEFAULT 1 CHECK (id = 1),
    config_json JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Live, versioned running-config history. Each HTTP commit appends a new row; the
-- highest version is the active configuration. Version 1 is seeded from the
-- startup-config on a factory-fresh database.
CREATE TABLE IF NOT EXISTS running_config (
    id           BIGSERIAL   PRIMARY KEY,
    version      BIGINT      NOT NULL UNIQUE,
    config_json  JSONB       NOT NULL,
    committed_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Runtime monitoring DATA (e.g. aggregated heartbeat results), keyed by daemon.
CREATE TABLE IF NOT EXISTS state_snapshot (
    daemon      TEXT PRIMARY KEY,
    snapshot    JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- SNMP/ICMP device inventory.
CREATE TABLE IF NOT EXISTS devices (
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
