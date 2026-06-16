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

-- Runtime monitoring DATA (e.g. aggregated heartbeat results), keyed by daemon.
CREATE TABLE IF NOT EXISTS state_snapshot (
    daemon      TEXT PRIMARY KEY,
    snapshot    JSONB NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
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

-- Unified per-IP device inventory for the probe pipeline (ICMP reachability -> SNMP
-- -> vendor API). One row per IP; each column is owned by the stage that writes it.
-- Replaces the former split icmp_devices / snmp_devices tables. Written exclusively by
-- engined (the single DB writer): ProbeService owns the ICMP columns + row lifecycle,
-- ScanService owns the SNMP/API columns. mgmtd reads it for /api/devices.
--   interfaces     : IP-bearing interfaces (ipAddrTable / vendor API) — {ip, netmask, if_index, if_name}
--   if_table       : IF-MIB ifTable/ifXTable rows (ports/NICs)
--   lldp_neighbors : LLDP neighbors (topology edges)
--   arp_entries    : ipNetToMedia IP↔MAC the device has learned
--   host_vendor    : OUI vendor for the ARP-learned MAC (ICMP-only hosts)
--   snmp_vendor    : resolved from sysObjectID enterprise number (by engined)
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
    -- the `? 'snmpd'` guard is false. daemon_map's stale nested key is unused by code.
    UPDATE running_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('scand', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    UPDATE startup_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('scand', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
END $migrate$;
