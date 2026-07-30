-- shared/db/schema.sql
--
-- Canonical DDL for the pretzel config/state store. This standalone script is
-- mirrored by kSchemaDDL in shared/db/Database.cpp (keep the two in sync). Every
-- statement is idempotent (IF NOT EXISTS), so it is safe to apply on every boot.
--
-- Two-table configuration model (commercial network-gear style):
--   startup_config : baseline boot config (the shipped startup-config file).
--   running_config : live, versioned config history (the active running-config).

-- Identity-column naming: every configuration object has exactly ONE identity, `oid` — a UUID
-- string issued at creation and immutable for the object's lifetime (pretzel calls it oid; there
-- is no separate uuid/numeric-id pair). Internal singleton/serial row keys are also named `oid`.
-- No table carries a bare `id`.

-- Baseline boot config. Synced from /etc/pretzel/startup-config.json by mgmtd at
-- boot. Singleton row (oid = 1).
CREATE TABLE IF NOT EXISTS startup_config (
    oid         INT PRIMARY KEY DEFAULT 1 CHECK (oid = 1),
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
    oid          BIGSERIAL   PRIMARY KEY,
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

-- Upgrade path for databases created when the persistent tables still carried `id`
-- (projections like inventory are drop+recreated, so only these two need renaming).
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

-- Config-json normalization for the single-identity merge: objects carried `uuid` (and, on auth
-- profiles / sites / connectors, a separate numeric `oid`). Fold uuid into oid and drop the
-- numeric one, across every persisted version and the baseline. Idempotent — once folded, the
-- `? 'uuid'` guards are false.
DO $merge_oid$
DECLARE
    tbl  TEXT;
    spec TEXT;
    path TEXT[];
BEGIN
    FOREACH tbl IN ARRAY ARRAY['running_config', 'startup_config'] LOOP
        FOREACH spec IN ARRAY ARRAY['probed.service.probe.probe_targets',
                                    'collectord.service.api.auth_profiles',
                                    'collectord.service.api.connectors',
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
-- device_credentials was an abandoned first pass at the encrypted credential store: no DDL, no
-- reader, no writer, and it survived every reset because nothing listed it. It held cipher text
-- nothing could decrypt, so it goes. api_credential_state/api_endpoint_state were declared before they
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

-- Devices projected from running_config. NGFW and SASE are separate tables because they carry
-- different access details and health mechanisms; the table (like the config array) IS the type,
-- so there is no device_type discriminator. Each row is a hybrid — fields projected from config
-- (site/target/name/description + the type-specific bits) plus live runtime state (status/last_seen).
DROP TABLE IF EXISTS inventory;
DROP TABLE IF EXISTS devices;   -- legacy mixed table, replaced by the two below
--   oid    : object identity — a UUID string, immutable
--   site   : oid of the site the device belongs to ('' = unassigned)
--   target : mgmt IP / FQDN, reached directly; fingerprint pins its TLS cert (from the API Key test)
CREATE TABLE IF NOT EXISTS ngfw_device (
    oid         TEXT PRIMARY KEY,
    site        TEXT,
    target      TEXT,
    name        TEXT,
    description TEXT,
    fingerprint TEXT,
    status      TEXT,
    last_seen   TIMESTAMPTZ,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX IF NOT EXISTS ngfw_device_target_uniq ON ngfw_device (target)
    WHERE target IS NOT NULL AND target <> '';
-- A cloud tenant, reached through its control-plane API. target is the tenant/TSG id. The
-- getPrismaAccessIP api-key is a secret, so it lives here (api_key_enc, AES-256-GCM, DB-only,
-- written by engined) and never in running_config; health_url/health_body are the operator-declared
-- probe request; egress_result caches the last getPrismaAccessIP response (zones + egress IPs).
CREATE TABLE IF NOT EXISTS sase_device (
    oid           TEXT PRIMARY KEY,
    site          TEXT,
    target        TEXT,
    name          TEXT,
    description   TEXT,
    health_url    TEXT,
    health_body   TEXT,
    api_key_enc   TEXT,
    status        TEXT,
    last_seen     TIMESTAMPTZ,
    egress_result JSONB,
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX IF NOT EXISTS sase_device_target_uniq ON sase_device (target)
    WHERE target IS NOT NULL AND target <> '';

-- Split the old mixed devices[] config array into type-specific arrays (the array now IS the type,
-- so device_type is dropped). The SASE api-key that used to sit in config is dropped here too — it
-- moves to sase_device.api_key_enc, re-entered via the UI (a SQL migration cannot seal it).
DO $split_devices$
DECLARE tbl TEXT;
BEGIN
    FOREACH tbl IN ARRAY ARRAY['running_config', 'startup_config'] LOOP
        EXECUTE format($fmt$
            UPDATE %I SET config_json = jsonb_set(
                jsonb_set(
                    config_json #- '{engined,service,site,devices}',
                    '{engined,service,site,ngfw_devices}',
                    COALESCE((SELECT jsonb_agg(e - 'device_type')
                              FROM jsonb_array_elements(config_json #> '{engined,service,site,devices}') e
                              WHERE e->>'device_type' IS DISTINCT FROM 'sase'), '[]'::jsonb)),
                '{engined,service,site,sase_devices}',
                COALESCE((SELECT jsonb_agg((e - 'device_type') - 'api_key')
                          FROM jsonb_array_elements(config_json #> '{engined,service,site,devices}') e
                          WHERE e->>'device_type' = 'sase'), '[]'::jsonb))
            WHERE config_json #> '{engined,service,site}' ? 'devices'
        $fmt$, tbl);
    END LOOP;
END $split_devices$;

-- What pretzel learns about a device API key, as opposed to what the operator declared. The
-- declaration (name, device, endpoint, account) lives in running_config; the issued secret and
-- its verification history live here, because running_config is append-versioned, shown verbatim
-- in the review diff and exported by Save-to-file — a key written there would be permanent,
-- readable by every reviewer, and would mint a configuration version each time it was re-issued.
-- Same reasoning that keeps admin passwords in local_users.
--
-- Written only by engined; the values arrive already sealed over IPC (collectord seals them with
-- /etc/pretzel/credentials.key, the one process that holds a plaintext credential). Keyed by the
-- API Key oid. A single schema serves both device types: for ngfw the durable secret is the issued
-- key; for sase it is the tenant OAuth credential (the bearer token stays ephemeral in memory).
--   id_enc     : account identity  — ngfw username / sase client id     (AES-256-GCM, base64)
--   pw_enc     : account secret    — ngfw password / sase client secret (AES-256-GCM, base64)
--   key_enc : issued key/token  — AES-256-GCM, base64(nonce ‖ tag ‖ ciphertext). A database copy
--                without credentials.key is useless.
--   expires_at : NULL means no expiry — PAN-OS keys are indefinite unless an API key lifetime is
--                configured on the device.
-- Rename from the pre-"credential" table name, preserving rows, before the CREATE below no-ops.
DO $rename_credstate$
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.tables WHERE table_name = 'api_key_state')
       AND NOT EXISTS (SELECT 1 FROM information_schema.tables WHERE table_name = 'api_credential_state') THEN
        ALTER TABLE api_key_state RENAME TO api_credential_state;
    END IF;
END $rename_credstate$;
CREATE TABLE IF NOT EXISTS api_credential_state (
    oid            TEXT PRIMARY KEY,
    id_enc         TEXT,
    pw_enc         TEXT,
    key_enc     TEXT,
    issued_at      TIMESTAMPTZ,
    expires_at     TIMESTAMPTZ,
    last_test_at   TIMESTAMPTZ,
    last_test_ok   BOOLEAN,
    last_test_note TEXT,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);
-- Upgrade path for databases created before the credential columns existed.
ALTER TABLE api_credential_state ADD COLUMN IF NOT EXISTS id_enc TEXT;
ALTER TABLE api_credential_state ADD COLUMN IF NOT EXISTS pw_enc TEXT;
-- secret_enc was renamed to key_enc (it holds an issued key/token, not a generic secret).
DO $rename_keyenc$
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_name = 'api_credential_state' AND column_name = 'secret_enc')
       AND NOT EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_name = 'api_credential_state' AND column_name = 'key_enc') THEN
        ALTER TABLE api_credential_state RENAME COLUMN secret_enc TO key_enc;
    END IF;
END $rename_keyenc$;

-- API collection samples: what each connector's scheduled endpoint poll returned. Pure state
-- (system-produced, never operator-declared), written only by engined from collectord's IPC — the same
-- config-vs-state split that keeps issued keys out of running_config. Raw response + call metadata
-- now; structured metric extraction is a later analytics layer that reads these rows back.
--   connector_oid/endpoint_oid : which connector schedule, and which of its endpoints, this is from
--   ok        : the poll produced a usable response (HTTP 200)
--   body      : the response, capped; oversized replies are cut and `truncated` is set
CREATE TABLE IF NOT EXISTS api_collection (
    oid           BIGSERIAL   PRIMARY KEY,
    connector_oid TEXT        NOT NULL,
    endpoint_oid  TEXT        NOT NULL,
    collected_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    ok            BOOLEAN     NOT NULL,
    http_status   INT,
    latency_ms    INT,
    bytes         INT,
    truncated     BOOLEAN     NOT NULL DEFAULT false,
    body          TEXT,
    error         TEXT
);
-- Time-series read paths: latest samples for a connector, and one endpoint's history.
CREATE INDEX IF NOT EXISTS api_collection_conn_time ON api_collection (connector_oid, collected_at DESC);
CREATE INDEX IF NOT EXISTS api_collection_endpoint_time ON api_collection (endpoint_oid, collected_at DESC);

-- System logs: a structured, queryable copy of each daemon's spdlog file. engined tails the rotating
-- log files from a checkpoint (system_log_offset) and batch-inserts parsed rows here — the files stay
-- as local durability, this table is the index the web UI reads. All parsing, ANSI stripping and
-- multi-line folding happens once at ingest, so the frontend renders clean rows without parsing logs.
--   level   : spdlog severity — 0=trace 1=debug 2=info 3=warn 4=error 5=critical
--   loc     : source location "file.cpp:line" when the line carried one
--   message : the log text, ANSI-stripped; continuation lines of a multi-line entry are folded in
CREATE TABLE IF NOT EXISTS system_log (
    oid     BIGSERIAL   PRIMARY KEY,
    ts      TIMESTAMPTZ NOT NULL,
    daemon  TEXT        NOT NULL,
    level   SMALLINT    NOT NULL,
    loc     TEXT,
    message TEXT        NOT NULL
);
-- Reads are always "newest first, filtered": keyset-paginate on oid DESC, optionally narrowed by
-- daemon or severity. oid order matches insert (hence time) order, so it doubles as the paging cursor.
CREATE INDEX IF NOT EXISTS system_log_oid        ON system_log (oid DESC);
CREATE INDEX IF NOT EXISTS system_log_daemon_oid ON system_log (daemon, oid DESC);
CREATE INDEX IF NOT EXISTS system_log_level_oid  ON system_log (level, oid DESC);

-- Tailer checkpoint: how far into each daemon's current log file engined has already ingested.
-- inode detects rotation (spdlog renames the base file, so a new inode appears) — on mismatch the
-- offset resets to 0 instead of skipping the fresh file; a size < offset (truncation) resets too.
CREATE TABLE IF NOT EXISTS system_log_offset (
    daemon     TEXT        PRIMARY KEY,
    inode      BIGINT      NOT NULL,
    offset_b   BIGINT      NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- One-time config-json normalizations (idempotent; run by engined via Config::preflight).
DO $migrate$
BEGIN
    -- Daemon rename (snmpd -> collectord): move the top-level config section so the renamed
    -- daemon finds its settings across every running_config version and the startup_config
    -- baseline. Idempotent — once moved, the `? 'snmpd'` guard is false.
    UPDATE running_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('collectord', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    UPDATE startup_config SET config_json =
        (config_json - 'snmpd') || jsonb_build_object('collectord', config_json->'snmpd')
        WHERE config_json ? 'snmpd';
    -- Drop the dead ipcd.service.daemon_map: routing uses the compiled IpcDaemon enum,
    -- never this config key. Strip the stale nested key from every persisted version.
    UPDATE running_config SET config_json = config_json #- '{ipcd,service,daemon_map}'
        WHERE config_json #> '{ipcd,service}' ? 'daemon_map';
    UPDATE startup_config SET config_json = config_json #- '{ipcd,service,daemon_map}'
        WHERE config_json #> '{ipcd,service}' ? 'daemon_map';
    -- API key -> credential rename: move collectord.service.api.api_keys to .api_credentials in every
    -- persisted version and the baseline. Idempotent — once moved, the `? 'api_keys'` guard is false.
    UPDATE running_config SET config_json =
        jsonb_set(config_json, '{collectord,service,api,api_credentials}', config_json #> '{collectord,service,api,api_keys}', true)
            #- '{collectord,service,api,api_keys}'
        WHERE config_json #> '{collectord,service,api}' ? 'api_keys';
    UPDATE startup_config SET config_json =
        jsonb_set(config_json, '{collectord,service,api,api_credentials}', config_json #> '{collectord,service,api,api_keys}', true)
            #- '{collectord,service,api,api_keys}'
        WHERE config_json #> '{collectord,service,api}' ? 'api_keys';
END $migrate$;
