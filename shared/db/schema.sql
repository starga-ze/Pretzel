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

-- ICMP reachability inventory (renamed from `devices` for symmetry with
-- snmp_devices). SNMP-discovered attributes live in snmp_devices; this table is the
-- ICMP side. Rename migration is idempotent.
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

-- Local login accounts (operator credentials), stored hashed (SHA-256 of
-- password+salt). A dedicated, NON-versioned store — kept out of running_config so
-- password changes never create config-history versions, and out of cleartext on
-- disk. Keyed by username so it extends to multiple local users / a future CLI daemon.
-- engined (the single DB writer) seeds the default admin and applies password changes;
-- must_change forces a change off the factory default on first login.
DROP TABLE IF EXISTS admin_user;  -- legacy
CREATE TABLE IF NOT EXISTS local_users (
    username      TEXT PRIMARY KEY,
    password_hash TEXT NOT NULL,
    salt          TEXT NOT NULL,
    must_change   BOOLEAN NOT NULL DEFAULT true,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- SNMP scan results, kept separate from the ICMP-discovered `devices` inventory so
-- the two write paths never clobber each other. Keyed by IP; the interface MAC set
-- (ifPhysAddress) is stored as a JSON array for hardware fingerprinting. Written
-- exclusively by engined (the single DB writer); mgmtd reads it for /api/devices.
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
