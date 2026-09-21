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

-- ── The AI assistant's sealed keys, and the model catalog they fetch ────────────
-- Mirrored from kSchemaDDL in shared/db/Database.cpp, which is what actually runs at boot. These
-- three were missing here until 2026-09-21 — the file claimed a mirror it was not keeping, which
-- is worse than no mirror at all: a reader consults this to learn the shape of the store and would
-- have concluded the appliance has nowhere to put a vendor key.

CREATE TABLE IF NOT EXISTS ai_provider_credential_state (
    id             TEXT PRIMARY KEY CHECK (id IN ('openai', 'google', 'anthropic')),
    key_enc        TEXT,            -- AES-256-GCM, base64(nonce ‖ tag ‖ ciphertext)
    last_test_at   TIMESTAMPTZ,
    last_test_ok   BOOLEAN,
    last_test_note TEXT,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Upgrade path for the CHECK above, and for two constraint names left behind by earlier renames.
--
-- The vendor list is closed the same way the route's pair is: a provider whose endpoint is not
-- compiled into pretzel-ai cannot serve a turn, so a row for one is key material nothing can spend.
-- One such row was found on 2026-09-07 — 'claude', left over from before the provider was renamed
-- to 'anthropic' — and it was invisible to the console, which filters the credential endpoint by
-- the same list. engined now prunes these on every commit; the constraint is what stops one being
-- written in the first place, and it is the asymmetry with the table below that let it happen.
--
-- Violating rows are DELETEd rather than left for the ALTER to trip over. A constraint that cannot
-- be added fails ensureSchema, which fails engined's preflight, which stops the appliance booting —
-- and the rows it would trip over are, by the definition the constraint states, meaningless.
DO $ai_provider_cred_upgrade$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ai_gateway_credential_state_pkey') THEN
        ALTER TABLE ai_provider_credential_state
            RENAME CONSTRAINT ai_gateway_credential_state_pkey TO ai_provider_credential_state_pkey;
    END IF;

    IF NOT EXISTS (SELECT 1 FROM pg_constraint
                    WHERE conname = 'ai_provider_credential_state_id_check') THEN
        DELETE FROM ai_provider_credential_state WHERE id NOT IN ('openai', 'google', 'anthropic');
        ALTER TABLE ai_provider_credential_state
            ADD CONSTRAINT ai_provider_credential_state_id_check
            CHECK (id IN ('openai', 'google', 'anthropic'));
    END IF;
END
$ai_provider_cred_upgrade$;

-- Renamed from ai_guardrail_credential_state on 2026-09-07, when the console page it belongs to
-- became AI Route. RENAME rather than a new table plus a copy: the rows hold sealed key material,
-- and a migration that re-inserts them is a migration that can half-succeed and leave an appliance
-- with a key it can no longer open. Guarded both ways so it is a no-op on a fresh database (no old
-- table) and on one already migrated (new table present), and it must stay AHEAD of the CREATE
-- below — running that first would make an empty table for the rename to refuse.
DO $ai_route_cred_rename$
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.tables
                WHERE table_schema = current_schema() AND table_name = 'ai_guardrail_credential_state')
       AND NOT EXISTS (SELECT 1 FROM information_schema.tables
                        WHERE table_schema = current_schema() AND table_name = 'ai_route_credential_state')
    THEN
        ALTER TABLE ai_guardrail_credential_state RENAME TO ai_route_credential_state;
        -- RENAME TO does not carry the constraint names with it, and a table whose primary key
        -- still says ai_guardrail_ reads in \\d as though the rename half happened.
        ALTER TABLE ai_route_credential_state
            RENAME CONSTRAINT ai_guardrail_credential_state_pkey TO ai_route_credential_state_pkey;
        ALTER TABLE ai_route_credential_state
            RENAME CONSTRAINT ai_guardrail_credential_state_id_check TO ai_route_credential_state_id_check;
    END IF;
END
$ai_route_cred_rename$;

-- The route's API keys, sealed the same way and for the same reasons as the providers' above.
--
-- Its own table rather than a reserved id in that one. The two are the same shape and could have
-- shared, but they are not the same kind of thing: a provider row is one of a set an operator adds
-- to and removes from, and these are single facts about this appliance — there is one scan service
-- and one gateway account, and a second row for either would not mean anything. Sharing would also
-- have made every query that means "the vendors" carry a filter to exclude the rows that are not
-- vendors, which is the shape of bug that gets written once and found much later.
--
-- Two rows at most, and the check says which: 'airs' is the scan service's subscription, 'portkey'
-- the AI gateway's. Both are configured on the same console page and both are a single fact about
-- this appliance rather than one of a set, which is what separates them from the vendors next door.
-- Enumerated rather than left open so a caller that thought it was writing a keyed store cannot
-- invent a third id nothing downstream reads.
CREATE TABLE IF NOT EXISTS ai_route_credential_state (
    id             TEXT PRIMARY KEY CHECK (id IN ('airs', 'portkey')),
    key_enc        TEXT,            -- AES-256-GCM, base64(nonce ‖ tag ‖ ciphertext)
    last_test_at   TIMESTAMPTZ,
    last_test_ok   BOOLEAN,
    last_test_note TEXT,
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- The models each vendor's account actually serves, as the vendor's own list endpoint last
-- answered. Pure state, and the config-vs-state line runs exactly where it does for the tables
-- above: the operator declares WHICH models this appliance may ask for — that is providers.list in
-- running_config — and these rows are the menu they choose from. Writing the menu into
-- running_config would mint a configuration version every time a vendor shipped a model, and would
-- put a fact nobody authored into the operator's review diff.
--
-- Filled by collectord, which owns every outbound vendor call, and written by engined, which owns
-- every table. mgmtd only reads it: the console's model picker is a SELECT against these rows
-- rather than a list shipped inside its JavaScript, which is the thing that went stale between
-- releases and could only be corrected by a release.
--
-- Keyed on (provider, model_id) and carrying no oid. Every configuration object has one, because
-- an operator created it and may rename it; a row here is the vendor's own name for something the
-- vendor owns, so there is no identity for an oid to preserve. Re-fetching a vendor replaces its
-- rows, and a model that came back under the same name is the same row rather than a new one.
--
--   label        the vendor's display name where they give one (Anthropic display_name, Gemini
--                displayName). OpenAI gives none, so it falls back to model_id.
--   token_param  which name the output cap goes out under when pretzel-ai calls this model. NOT
--                from the vendor — none of the three report it — so collectord derives it while
--                refining the list. A column rather than a compiled-in map because a derivation
--                that guesses wrong fails the turn, and an operator must be able to correct it
--                without waiting for a release.
--   fetched_at   when the vendor last answered for this row. A vendor whose fetch fails keeps the
--                rows it had, so this is also how the console says how stale a menu is.
CREATE TABLE IF NOT EXISTS ai_provider_model (
    provider     TEXT        NOT NULL CHECK (provider IN ('openai', 'google', 'anthropic')),
    model_id     TEXT        NOT NULL,
    label        TEXT,
    token_param  TEXT,
    fetched_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (provider, model_id)
);

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
