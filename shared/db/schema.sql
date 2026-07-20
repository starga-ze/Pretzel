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
-- highest 'active' version is the live configuration. Version 1 is seeded from the
-- startup-config on a factory-fresh database.
-- state drives the config-version convergence model: a commit appends a 'pending'
-- row; once every service daemon reports applied_version >= this version, engined
-- promotes it to 'active' (and the prior active becomes 'superseded'). A daemon that
-- cold-restarts mid-reload loads the highest 'active' (last-good) version, never the
-- in-flight 'pending' one.
CREATE TABLE IF NOT EXISTS running_config (
    id           BIGSERIAL   PRIMARY KEY,
    version      BIGINT      NOT NULL UNIQUE,
    config_json  JSONB       NOT NULL,
    committed_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    state        TEXT        NOT NULL DEFAULT 'active'
        CONSTRAINT running_config_state_check CHECK (state IN ('pending','active','superseded'))
);
-- Upgrade path for databases created before the state column existed.
ALTER TABLE running_config ADD COLUMN IF NOT EXISTS state TEXT NOT NULL DEFAULT 'active';
DO $rc_state$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'running_config_state_check') THEN
        ALTER TABLE running_config
            ADD CONSTRAINT running_config_state_check CHECK (state IN ('pending','active','superseded'));
    END IF;
END $rc_state$;

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

-- Legacy tables removed: probe_devices (mixed ICMP status + discovered SNMP/interface/
-- LLDP data) and state_snapshot (heartbeat snapshot written but never read).
DROP TABLE IF EXISTS probe_devices;
DROP TABLE IF EXISTS state_snapshot;

-- Managed inventory: the operator-declared objects. Source of truth is running-config;
-- engined (the single DB writer) projects the configured objects here on reload and updates
-- `status` from probe results. Holds only config attributes + live status. It is a pure
-- projection (rebuilt from config), so the DDL drops+recreates it to evolve the shape.
--   id       : object UUID (immutable)
--   platform : direct (IP-based, mgmt IP/FQDN) | tenant (cloud platform, tenant-scoped)
--   target   : endpoint identifier — direct: mgmt IP/FQDN · tenant: tenant/TSG id
DROP TABLE IF EXISTS inventory;
CREATE TABLE IF NOT EXISTS inventory (
    id          TEXT PRIMARY KEY,
    type        TEXT NOT NULL,
    platform    TEXT NOT NULL DEFAULT 'direct',
    target      TEXT,
    name        TEXT,
    description TEXT,
    enabled     BOOLEAN NOT NULL DEFAULT true,
    status      TEXT,
    last_seen   TIMESTAMPTZ,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- One object per connection identity, per platform (dup-prevention).
CREATE UNIQUE INDEX IF NOT EXISTS inventory_target_uniq ON inventory (platform, target)
    WHERE target IS NOT NULL AND target <> '';

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
