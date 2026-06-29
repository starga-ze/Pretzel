/* settings.js
 *
 * GET  /api/settings              → { daemons: { <svc>: { <domain>: { <key>: val } } } }
 * POST /api/settings/commit       → { reloading, applied, failed, results[] }
 * GET  /api/settings/reload-status→ { status: "idle"|"reloading"|"complete", elapsed_ms }
 *
 * Page is view-only. Clicking the ✏ edit button on a card opens a right-side
 * slide-over panel with editable fields. Apply stages changes; Commit deploys.
 */

(function () {
  'use strict';

  // ── Tab definitions ──────────────────────────────────────────────────────

  const TABS = {
    general: {
      label:    'General',
      title:    'General',
      subtitle: 'Global service behavior and health-check parameters.',
      daemons:  ['engined'],
      domains:  ['heartbeat'],
    },
    protocol: {
      label:    'Protocol',
      title:    'Protocol',
      subtitle: 'Discovery protocol configuration.',
      protocols: ['icmp', 'snmp', 'lldp'],
    },
    rack: {
      label:    'Rack List',
      title:    'Rack List',
      subtitle: 'Define racks (name + description). Assign devices in Resource ▸ Rack Management.',
      daemons:  [],
      domains:  [],
    },
    users: {
      label:    'User',
      title:    'User Management',
      subtitle: 'Manage login accounts and access permissions.',
      daemons:  [],
      domains:  [],
    },
  };

  // Sub-tabs shown on the Protocol page (right of the nav). Each maps to the
  // daemon/domain whose config it exposes; an empty daemon list renders an empty state.
  const PROTOCOLS = {
    icmp: {
      label:    'ICMP',
      subtitle: 'Scan ranges, excluded IPs, and probe timing for ICMP ping discovery.',
      daemons:  ['icmpd'],
      domains:  ['probe'],
    },
    snmp: {
      label:    'SNMP',
      subtitle: 'Global polling parameters and the per-device scan table (SNMPv2c / SNMPv3 / API).',
      daemons:  ['scand'],
      domains:  ['scan'],
    },
    lldp: {
      label:    'LLDP',
      subtitle: 'Link Layer Discovery Protocol neighbor detection.',
      daemons:  [],
      domains:  [],
    },
  };

  const LIST_KEYS   = new Set(['scan_cidr', 'excluded_ips']);
  const STRING_KEYS = new Set(['scan_cidr', 'excluded_ips']);

  const KEY_LABELS = {
    scan_cidr:                 'Scan CIDRs',
    excluded_ips:              'Excluded IPs',
    batch_size:                 'Batch Size',
    batch_interval_ms:          'Batch Interval (ms)',
    cycle_interval_sec:         'Cycle Interval (s)',
    reply_idle_timeout_sec:     'Reply Idle Timeout (s)',
    reply_max_wait_timeout_sec: 'Max Reply Wait (s)',
    poll_interval_sec:          'Poll Interval (s)',
    response_timeout_sec:       'Response Timeout (s)',
    // SNMP (scand/scan)
    port:                       'Port',
    timeout_sec:                'Poll Timeout (s)',
    retries:                    'Poll Retries',
    max_concurrent:             'Max Concurrent',
    v2c_probe_timeout_ms:       'Reachability Probe Timeout (ms)',
    v2c_probe_retries:          'Reachability Probe Retries',
  };

  // One-line descriptions shown in the table's Description column (view-only context).
  const KEY_DESC = {
    scan_cidr:                  'CIDR blocks scanned for active hosts.',
    excluded_ips:               'Addresses skipped even if inside a scan CIDR.',
    batch_size:                 'Number of probes sent per batch.',
    batch_interval_ms:          'Delay between consecutive probe batches.',
    cycle_interval_sec:         'Pause between full discovery cycles.',
    reply_idle_timeout_sec:     'Stop waiting once no replies arrive for this gap.',
    reply_max_wait_timeout_sec: 'Hard cap on waiting for replies in a cycle.',
    poll_interval_sec:          'How often engined polls daemon health.',
    response_timeout_sec:       'Timeout waiting for a daemon heartbeat reply.',
    port:                       'UDP port for SNMP requests.',
    timeout_sec:                'Timeout for the actual data-gathering poll, once a device is known reachable.',
    retries:                    'Retry attempts for the data-gathering poll after a timeout.',
    max_concurrent:             'Maximum simultaneous SNMP sessions.',
    v2c_probe_timeout_ms:       'Timeout for the quick "is this host alive over v2c?" check that runs before the full poll.',
    v2c_probe_retries:          'Retries for that quick reachability check, separate from the poll retries above.',
  };

  const KEY_PLACEHOLDER = {
    scan_cidr:    'e.g. 192.168.0.0/23',
    excluded_ips: 'e.g. 192.168.0.1',
  };

  const KEY_HINT = {
    scan_cidr:    'Add one CIDR block per entry. Multiple CIDRs are all scanned and deduplicated.',
    excluded_ips: 'IPs listed here are skipped even if they fall within a scan CIDR.',
  };

  const DOMAIN_LABELS = { probe: 'Probe', heartbeat: 'Heartbeat', scan: 'Polling' };

  const PROBE_TARGET_KEYS = new Set(['scan_cidr', 'excluded_ips']);
  const PROBE_TIMING_KEYS = new Set([
    'batch_size', 'batch_interval_ms', 'cycle_interval_sec',
    'reply_idle_timeout_sec', 'reply_max_wait_timeout_sec',
  ]);

  // SNMPv3 fallback credentials (scand/scan). `v3` is the global default; `v3_devices`
  // is a per-IP override list. Both are nested under the scan domain and are handled by
  // a dedicated editor (the flat scalar pipeline can't represent them).
  const V3_OBJECT_KEY  = 'v3';
  const V3_DEVICES_KEY = 'v3_devices';
  // Field spec for one v3 credential set, rendered by the v3 editor in order.
  const V3_FIELDS = [
    { key: 'user',           label: 'User',            type: 'text' },
    { key: 'security_level', label: 'Security Level',  type: 'select',
      options: ['authPriv', 'authNoPriv', 'noAuthNoPriv'] },
    { key: 'auth_protocol',  label: 'Auth Protocol',   type: 'select', options: ['SHA', 'MD5'] },
    { key: 'auth_password',  label: 'Auth Password',   type: 'password' },
    { key: 'priv_protocol',  label: 'Priv Protocol',   type: 'select', options: ['AES', 'DES'] },
    { key: 'priv_password',  label: 'Priv Password',   type: 'password' },
  ];
  const V3_DEFAULTS = {
    user: '', security_level: 'authPriv',
    auth_protocol: 'SHA', auth_password: '', priv_protocol: 'AES', priv_password: '',
  };

  // Vendor-API credentials (scand/scan, key `api_devices`) — the "API" scan method,
  // run entirely independently of v2c/v3 by its own engine. A device listed here is
  // queried over its vendor HTTP API to fill the topology data (interface IPs, ARP,
  // LLDP) its SNMP agent doesn't expose (e.g. PAN-OS). Per-IP array, edited like
  // v3_devices.
  const API_DEVICES_KEY = 'api_devices';
  const API_FIELDS = [
    { key: 'vendor',     label: 'Vendor',             type: 'select', options: ['paloalto'] },
    { key: 'username',   label: 'Username',           type: 'text' },
    { key: 'password',   label: 'Password',           type: 'password' },
    { key: 'port',       label: 'Port',               type: 'text' },
    { key: 'api_key',    label: 'API Key (optional)', type: 'password' },
    { key: 'verify_tls', label: 'Verify TLS',         type: 'select', options: ['false', 'true'] },
  ];
  const API_DEFAULTS = {
    vendor: 'paloalto', username: '', password: '',
    port: '443', api_key: '', verify_tls: 'false',
  };

  // The committed JSON needs proper types (the backend reads port/verify_tls as
  // number/bool), but form inputs are strings — coerce on the way out.
  function coerceApiCred(c) {
    const out = Object.assign({}, c);
    out.port       = Number(c.port) || 443;
    out.verify_tls = (c.verify_tls === true || c.verify_tls === 'true');
    return out;
  }

  // Per-IP v2c community overrides (scand/scan, key `v2c_devices`). A device here
  // is probed with its own community instead of the global one. New: lets v2c be
  // managed per-device alongside v3/api in the unified Scan Devices table.
  const V2C_DEVICES_KEY = 'v2c_devices';
  const V2C_FIELDS = [
    { key: 'community', label: 'Community', type: 'select-or-custom', options: ['public', 'private'] },
  ];
  const V2C_DEFAULTS = { community: '' };

  // ── Unified Scan Devices model ─────────────────────────────────────────────
  // Each device picks exactly one scan method (v2c / v3 / api) here — there's no
  // fallback chain between them; scand runs SnmpEngine (v2c/v3) and ApiEngine (api)
  // as two independent engines and merges their results. Rather than three
  // separate tables, one "Scan Devices" table lists every device; each row's
  // `method` selects which underlying config array it lives in and which
  // parameter set applies. METHODS is the single source of truth tying a method
  // id to its array key, field spec, defaults, and the row-summary renderer.
  const METHODS = {
    v2c: {
      label:   'SNMPv2c',
      arrayKey: V2C_DEVICES_KEY,
      fields:   V2C_FIELDS,
      defaults: V2C_DEFAULTS,
      // What the Details column shows for this method. public/private aren't real
      // secrets (they're the well-known SNMP defaults) so show them plainly; only
      // mask a custom community string.
      summary: d => {
        const c = String(d.community || '');
        if (!c) return [['Community', '—']];
        const isPreset = c === 'public' || c === 'private';
        return [['Community', isPreset ? c : '••••••']];
      },
    },
    v3: {
      label:   'SNMPv3',
      arrayKey: V3_DEVICES_KEY,
      fields:   V3_FIELDS,
      defaults: V3_DEFAULTS,
      summary: d => [
        ['User',     d.user || '(no user)'],
        ['Security', d.security_level || 'authPriv'],
      ],
    },
    api: {
      label:   'API',
      arrayKey: API_DEVICES_KEY,
      fields:   API_FIELDS,
      defaults: API_DEFAULTS,
      summary: d => [
        ['Vendor', d.vendor || '(no vendor)'],
        ['Auth',   d.api_key ? 'API key' : (d.password ? 'Username/password' : 'No credential')],
      ],
    },
  };
  const METHOD_ORDER = ['v2c', 'v3', 'api'];

  // Reverse map: which method owns a given array key (used when regrouping).
  const KEY_TO_METHOD = {
    [V2C_DEVICES_KEY]: 'v2c',
    [V3_DEVICES_KEY]:  'v3',
    [API_DEVICES_KEY]: 'api',
  };

  // ── Validation helpers ─────────────────────────────────────────────────────
  function isValidIp(s) {
    const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(String(s).trim());
    if (!m) return false;
    return m.slice(1).every(o => { const n = Number(o); return n >= 0 && n <= 255; });
  }
  function isValidPort(s) {
    const n = Number(s);
    return Number.isInteger(n) && n >= 1 && n <= 65535;
  }
  // Per-method required-field / format validation. Returns a {field: message} map.
  function validateScanDevice(dev, existingIps) {
    const errs = {};
    const ip = (dev.ip || '').trim();
    if (!ip)                       errs.ip = 'IP address is required.';
    else if (!isValidIp(ip))       errs.ip = 'Enter a valid IPv4 address (e.g. 192.168.1.10).';
    else if (existingIps.has(ip))  errs.ip = 'A device with this IP already exists.';

    if (dev.method === 'v2c') {
      if (!(dev.community || '').trim()) errs.community = 'Community string is required.';
    } else if (dev.method === 'v3') {
      if (!(dev.user || '').trim()) errs.user = 'User is required for SNMPv3.';
    } else if (dev.method === 'api') {
      if (!(dev.vendor || '').trim()) errs.vendor = 'Vendor is required.';
      if (!isValidPort(dev.port))     errs.port = 'Port must be 1–65535.';
      if (!(dev.api_key || '').trim() && !(dev.password || '').trim())
        errs.password = 'Provide an API key or a username/password.';
    }
    return errs;
  }

  // ── State ─────────────────────────────────────────────────────────────────

  let currentData      = null;
  let activeTab        = 'protocol';
  let activeProto      = 'icmp';     // active sub-tab when activeTab === 'protocol'
  let selectedQueueKey = null;

  // staged edits: `${daemon} ${domain} ${key}` → { daemon, domain, key, oldValue, newValue }
  const pending = new Map();

  // edit panel state
  let editContext = null;   // { daemon, domain, values }
  let editMode    = 'scalar';   // 'scalar' (key fields) | 'rack' (device allocation)
  // in-panel staged values (before Apply)
  const editDraft = new Map();  // key → value

  // Rack slide-over state. editMode is 'rackNew' (Add Rack form) or 'rackEdit' (device
  // allocation). rackAllocDraft is the in-panel working copy of a rack's devices[].
  let rackDevices = [];     // /api/devices inventory for the device dropdown (cached)
  let rackEditIdx = null;   // index of the rack being allocated
  let rackAllocDraft = [];  // [{ device, ip, unit, size }] staged in the panel before Apply

  // Scan Devices table view-state (persists across re-renders of the card).
  let sdSearch = '';                          // free-text filter (ip/method/details)
  let sdMethodFilter = 'all';                 // 'all' | 'v2c' | 'v3' | 'api'
  let sdSort = { col: 'ip', dir: 'asc' };     // sortable column + direction

  // list editor callback
  let listEditorCallback = null;

  // ── DOM refs ──────────────────────────────────────────────────────────────

  let container, statusEl, pageTitleEl, pageSubtitleEl;
  let discardBtn, reviewBtn, commitPendingBadge, commitPendingCount;
  let reviewOverlay, reviewCloseBtn, reviewCancelBtn;
  let commitBtn, commitStatus, commitProgressFill;
  let commitQueueList, queueTotalBadge, diffViewer, diffPanelHint;
  let taskQueueBar;           // floating task-queue status bar
  let taskQueuePollTimer = null;

  // ── Helpers ───────────────────────────────────────────────────────────────

  function stageKey(daemon, domain, key) { return `${daemon} ${domain} ${key}`; }
  function cssEsc(s) { return String(s).replace(/["\\]/g, '\\$&'); }

  async function fetchJSON(url, opts) {
    const r = await fetch(url, Object.assign({ credentials: 'same-origin' }, opts));
    if (r.status === 401) { window.location.href = '/index.html'; return null; }
    return r;
  }

  function setStatus(msg, kind) {
    if (!statusEl) return;
    statusEl.textContent = msg || '';
    statusEl.className = 'settings-status' + (kind ? ` ${kind}` : '');
  }

  function setCommitStatus(msg, kind) {
    if (!commitStatus) return;
    commitStatus.textContent = msg || '';
    commitStatus.className = 'modal-status' + (kind ? ` ${kind}` : '');
  }

  function getItems(v) {
    return String(v).split(',').map(s => s.trim()).filter(Boolean);
  }

  // ── View rendering ────────────────────────────────────────────────────────

  function buildViewTags(value) {
    const wrap = document.createElement('div');
    wrap.className = 'le-tags-wrap';
    const items = getItems(value);
    if (items.length === 0) {
      wrap.innerHTML = '<span class="le-tag-empty">None</span>';
    } else {
      items.forEach(item => {
        const t = document.createElement('span');
        t.className = 'le-tag';
        t.textContent = item;
        wrap.appendChild(t);
      });
    }
    return wrap;
  }

  // Pencil edit button used in table headers.
  function buildEditIconBtn(onEdit) {
    const editBtn = document.createElement('button');
    editBtn.className = 'btn btn-icon-only st-card-edit-btn';
    editBtn.title = 'Edit';
    editBtn.innerHTML =
      `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round">` +
      `<path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/>` +
      `<path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>`;
    editBtn.addEventListener('click', onEdit);
    return editBtn;
  }

  // View-only table: one row per setting (Setting / Value / Description). `onEdit`,
  // when supplied, renders a pencil in the header that opens the slide-over editor.
  function buildSettingsTable(title, keys, values, onEdit, daemon, domain) {
    const wrap = document.createElement('div');
    wrap.className = 'st-table-card';

    const head = document.createElement('div');
    head.className = 'st-table-head';
    const t = document.createElement('span');
    t.className = 'st-table-title';
    t.textContent = title;
    head.appendChild(t);
    if (onEdit) head.appendChild(buildEditIconBtn(onEdit));
    wrap.appendChild(head);

    const table = document.createElement('table');
    table.className = 'st-table';
    table.innerHTML =
      '<thead><tr>' +
      '<th class="st-th-key">Setting</th>' +
      '<th class="st-th-val">Value</th>' +
      '</tr></thead>';

    const tbody = document.createElement('tbody');
    keys.forEach(k => {
      if (values[k] === undefined) return;
      const tr = document.createElement('tr');

      const tdK = document.createElement('td');
      tdK.className = 'st-td-key';
      const name = document.createElement('span');
      name.textContent = KEY_LABELS[k] || k;
      tdK.appendChild(name);
      // Description is no longer a column — it surfaces as an (i) hover tooltip.
      if (KEY_DESC[k]) tdK.appendChild(buildInfoIcon(KEY_DESC[k]));

      const tdV = document.createElement('td');
      tdV.className = 'st-td-val';
      if (LIST_KEYS.has(k)) {
        tdV.appendChild(buildViewTags(values[k]));
      } else {
        const v = document.createElement('span');
        v.className = 'st-cell-scalar';
        v.textContent = String(values[k]);
        if (daemon && domain && pending.has(stageKey(daemon, domain, k))) {
          v.classList.add('st-view-modified');
        }
        tdV.appendChild(v);
      }

      tr.append(tdK, tdV);
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    wrap.appendChild(table);
    return wrap;
  }

  // Small (i) icon; the global tooltip (main.js) shows data-tip on hover/focus.
  function buildInfoIcon(text) {
    const i = document.createElement('span');
    i.className = 'st-info';
    i.setAttribute('data-tip', text);
    i.setAttribute('tabindex', '0');
    i.setAttribute('aria-label', text);
    i.innerHTML =
      `<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">` +
      `<circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>`;
    return i;
  }

  function buildDomainCard(daemon, domain, values) {
    const card = document.createElement('div');
    card.className = 'settings-domain-card st-table-wrap';
    card.dataset.daemon = daemon;
    card.dataset.domain = domain;

    const keys = Object.keys(values);

    if (domain === 'probe') {
      // Probe: two independently-editable tables (targets + timing).
      const targetKeys = keys.filter(k => PROBE_TARGET_KEYS.has(k));
      const timingKeys = keys.filter(k => PROBE_TIMING_KEYS.has(k));

      if (targetKeys.length) {
        card.appendChild(buildSettingsTable(
          'Scan Targets', targetKeys, values,
          () => openEditPanel(daemon, domain, values, 'ICMP — Scan Targets', targetKeys),
          daemon, domain));
      }
      if (timingKeys.length) {
        card.appendChild(buildSettingsTable(
          'Timing', timingKeys, values,
          () => openEditPanel(daemon, domain, values, 'ICMP — Timing', timingKeys),
          daemon, domain));
      }
    } else {
      // Scalar settings table. Nested credential keys (v3 object + the per-IP
      // device arrays) are excluded here and rendered by the Scan Devices table.
      // The global v2c `community` default is excluded too — every v2c device now
      // sets its own community in the Scan Devices table, so showing a second,
      // easy-to-miss "default" copy here is more confusing than useful.
      const scalarKeys = keys.filter(k =>
        k !== V3_OBJECT_KEY && k !== V3_DEVICES_KEY &&
        k !== API_DEVICES_KEY && k !== V2C_DEVICES_KEY && k !== 'community');
      card.appendChild(buildSettingsTable(
        DOMAIN_LABELS[domain] || domain, scalarKeys, values,
        () => openEditPanel(daemon, domain, values, null, scalarKeys),
        daemon, domain));

      // Unified per-device scan table (v2c / v3 / vendor-API in one place).
      if (domain === 'scan') {
        card.appendChild(buildScanDevicesTable(daemon, domain));
      }
    }

    // Pending indicator
    const hasPending = Array.from(pending.values()).some(p => p.daemon === daemon && p.domain === domain);
    card.classList.toggle('has-changes', hasPending);

    return card;
  }

  // ── SNMPv3 fallback editor (nested object / per-IP array) ──────────────────
  // The flat scalar pipeline can't represent v3 (object) or v3_devices (array), so
  // these are staged directly into `pending` with the whole object/array as the value
  // (groupPending/renderDiff/commit all handle non-scalar values via JSON).

  function originalComplex(daemon, domain, key) {
    return currentData?.daemons?.[daemon]?.[domain]?.[key];
  }

  // Set (or clear) a staged complex change WITHOUT re-rendering — lets a caller
  // stage several array keys (e.g. all three device arrays) before one re-render.
  function setComplexPending(daemon, domain, key, oldValue, newValue) {
    const sk = stageKey(daemon, domain, key);
    if (JSON.stringify(oldValue ?? null) === JSON.stringify(newValue ?? null)) {
      pending.delete(sk);
    } else {
      pending.set(sk, { daemon, domain, key, oldValue, newValue });
    }
  }

  // Re-render one domain card with all of its pending changes merged in.
  function reRenderDomainCard(daemon, domain) {
    if (!currentData || !container) return;
    const base = Object.assign({}, currentData?.daemons?.[daemon]?.[domain] || {});
    pending.forEach(c => {
      if (c.daemon === daemon && c.domain === domain) base[c.key] = c.newValue;
    });
    const card = container.querySelector(
      `.settings-domain-card[data-daemon="${cssEsc(daemon)}"][data-domain="${cssEsc(domain)}"]`);
    if (card) card.replaceWith(buildDomainCard(daemon, domain, base));
  }

  // Stage (or clear) a complex object/array change and re-render the card.
  function stageComplex(daemon, domain, key, oldValue, newValue) {
    setComplexPending(daemon, domain, key, oldValue, newValue);
    refreshPendingBadge();
    reRenderDomainCard(daemon, domain);
  }

  // ── Unified Scan Devices table ─────────────────────────────────────────────
  // One table for all three per-device scan methods. Each row is a device tagged
  // with a `method`; on save the rows are regrouped back into the three config
  // arrays (v2c_devices / v3_devices / api_devices). Edits stage directly into
  // `pending` (no separate slide-over) so the table itself is the editing surface
  // — toggling, adding, editing and removing all re-render in place.

  // Effective value of an array key = staged change if present, else live value.
  function effectiveArray(daemon, domain, key) {
    const staged = pending.get(stageKey(daemon, domain, key))?.newValue;
    const live   = currentData?.daemons?.[daemon]?.[domain]?.[key];
    const src    = staged ?? live ?? [];
    return Array.isArray(src) ? JSON.parse(JSON.stringify(src)) : [];
  }

  // Flatten the three config arrays into one device list, tagging each with its
  // method and a normalized `enabled` flag (default true for legacy rows).
  function loadScanDevices(daemon, domain) {
    const out = [];
    METHOD_ORDER.forEach(method => {
      effectiveArray(daemon, domain, METHODS[method].arrayKey).forEach(d => {
        out.push(Object.assign({}, d, { method, enabled: d.enabled !== false }));
      });
    });
    return out;
  }

  // Normalize one device row for persistence: keep ip + the chosen method's
  // fields, coerce API types, and emit `enabled` only when false (true is the
  // backend default, so this keeps untouched arrays byte-identical / undiffed).
  function cleanScanDevice(dev) {
    const m = METHODS[dev.method];
    const out = { ip: (dev.ip || '').trim() };
    if (dev.enabled === false) out.enabled = false;
    m.fields.forEach(f => { if (dev[f.key] !== undefined) out[f.key] = dev[f.key]; });
    return dev.method === 'api' ? coerceApiCred(out) : out;
  }

  // Regroup the unified list back into the three config arrays, stage each that
  // changed (vs the live original), then refresh badge + re-render once.
  function persistScanDevices(daemon, domain, list) {
    const groups = { v2c: [], v3: [], api: [] };
    list.forEach(d => { if (groups[d.method]) groups[d.method].push(cleanScanDevice(d)); });
    METHOD_ORDER.forEach(method => {
      const key = METHODS[method].arrayKey;
      setComplexPending(daemon, domain, key,
                        originalComplex(daemon, domain, key) || [], groups[method]);
    });
    refreshPendingBadge();
    reRenderDomainCard(daemon, domain);
  }

  // Set of IPs already in use, optionally excluding one row (the one being edited).
  function scanDeviceIps(list, exceptIdx) {
    const s = new Set();
    list.forEach((d, i) => { if (i !== exceptIdx && d.ip) s.add(String(d.ip).trim()); });
    return s;
  }

  // Sort IPs numerically by octet when valid, else lexically.
  function ipSortKey(ip) {
    if (isValidIp(ip)) return String(ip).trim().split('.').map(o => o.padStart(3, '0')).join('.');
    return String(ip || '');
  }

  function methodBadge(method) {
    const span = document.createElement('span');
    span.className = `sd-badge sd-badge-${method}`;
    span.textContent = METHODS[method]?.label || method;
    return span;
  }

  // Apply the active search / method-filter / sort to the list for display.
  // Returns [{ d, i }] keeping each device's original index for in-place mutation.
  function viewScanDevices(list) {
    let rows = list.map((d, i) => ({ d, i }));
    if (sdMethodFilter !== 'all') rows = rows.filter(r => r.d.method === sdMethodFilter);
    const q = sdSearch.trim().toLowerCase();
    if (q) {
      rows = rows.filter(r => {
        const details = METHODS[r.d.method].summary(r.d).map(([label, value]) => `${label} ${value}`).join(' ');
        const hay = `${r.d.ip} ${METHODS[r.d.method].label} ${details}`;
        return hay.toLowerCase().includes(q);
      });
    }
    const dir = sdSort.dir === 'desc' ? -1 : 1;
    rows.sort((a, b) => {
      let av, bv;
      if (sdSort.col === 'method')      { av = a.d.method; bv = b.d.method; }
      else if (sdSort.col === 'status') { av = a.d.enabled ? 0 : 1; bv = b.d.enabled ? 0 : 1; }
      else                              { av = ipSortKey(a.d.ip); bv = ipSortKey(b.d.ip); }
      if (av < bv) return -dir;
      if (av > bv) return dir;
      return 0;
    });
    return rows;
  }

  function buildScanDevicesTable(daemon, domain) {
    const list = loadScanDevices(daemon, domain);

    const wrap = document.createElement('div');
    wrap.className = 'st-table-card sd-card';

    // ── Header: title + count + Add ──
    const head = document.createElement('div');
    head.className = 'st-table-head';
    const titleWrap = document.createElement('div');
    titleWrap.className = 'sd-title-wrap';
    const t = document.createElement('span');
    t.className = 'st-table-title';
    t.textContent = 'Scan Devices';
    const count = document.createElement('span');
    count.className = 'sd-count';
    titleWrap.append(t, count);
    head.appendChild(titleWrap);

    const addBtn = document.createElement('button');
    addBtn.type = 'button';
    addBtn.className = 'btn btn-primary sd-add-btn';
    addBtn.innerHTML =
      '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.8" stroke-linecap="round">' +
      '<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg> Add Device';
    addBtn.addEventListener('click', () => {
      openScanDeviceEditor({
        title: 'Add Scan Device',
        device: null,
        existingIps: scanDeviceIps(list, -1),
        onSave: (dev) => { list.push(dev); persistScanDevices(daemon, domain, list); },
      });
    });
    head.appendChild(addBtn);
    wrap.appendChild(head);

    // ── Toolbar: search + method filter chips ──
    const toolbar = document.createElement('div');
    toolbar.className = 'sd-toolbar';

    const searchWrap = document.createElement('div');
    searchWrap.className = 'sd-search';
    searchWrap.innerHTML =
      '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">' +
      '<circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>';
    const search = document.createElement('input');
    search.type = 'text';
    search.className = 'sd-search-input';
    search.placeholder = 'Search IP, method, details…';
    search.value = sdSearch;
    search.addEventListener('input', () => { sdSearch = search.value; renderBody(); });
    searchWrap.appendChild(search);
    toolbar.appendChild(searchWrap);

    const filters = document.createElement('div');
    filters.className = 'sd-filters';
    const filterDefs = [['all', 'All']].concat(METHOD_ORDER.map(m => [m, METHODS[m].label]));
    filterDefs.forEach(([id, label]) => {
      const chip = document.createElement('button');
      chip.type = 'button';
      chip.className = 'sd-chip' + (sdMethodFilter === id ? ' active' : '');
      chip.dataset.filter = id;
      chip.textContent = label;
      chip.addEventListener('click', () => {
        sdMethodFilter = id;
        filters.querySelectorAll('.sd-chip').forEach(c =>
          c.classList.toggle('active', c.dataset.filter === id));
        renderBody();
      });
      filters.appendChild(chip);
    });
    toolbar.appendChild(filters);
    wrap.appendChild(toolbar);

    // ── Table ──
    const cols = [
      { id: 'status',  label: '',           sortable: true,  cls: 'sd-col-status' },
      { id: 'ip',      label: 'IP Address', sortable: true,  cls: 'sd-col-ip' },
      { id: 'method',  label: 'Method',     sortable: true,  cls: 'sd-col-method' },
      { id: 'details', label: 'Details',    sortable: false, cls: '' },
      { id: 'actions', label: '',           sortable: false, cls: 'sd-col-actions' },
    ];
    const table = document.createElement('table');
    table.className = 'st-table sd-table';
    const thead = document.createElement('thead');
    const htr = document.createElement('tr');
    cols.forEach(c => {
      const th = document.createElement('th');
      if (c.cls) th.className = c.cls;
      th.textContent = c.label;
      if (c.sortable) {
        th.classList.add('sd-th-sort');
        if (sdSort.col === c.id) th.classList.add(sdSort.dir === 'desc' ? 'sd-sort-desc' : 'sd-sort-asc');
        th.addEventListener('click', () => {
          if (sdSort.col === c.id) sdSort.dir = sdSort.dir === 'asc' ? 'desc' : 'asc';
          else { sdSort.col = c.id; sdSort.dir = 'asc'; }
          reRenderDomainCard(daemon, domain);   // refresh header arrows + order
        });
      }
      htr.appendChild(th);
    });
    thead.appendChild(htr);
    table.appendChild(thead);
    const tbody = document.createElement('tbody');
    table.appendChild(tbody);
    wrap.appendChild(table);

    function renderBody() {
      tbody.innerHTML = '';
      count.textContent = list.length ? String(list.length) : '';
      const rows = viewScanDevices(list);
      if (!rows.length) {
        const tr = document.createElement('tr');
        const td = document.createElement('td');
        td.colSpan = cols.length;
        td.className = 'st-td-empty';
        td.textContent = list.length
          ? 'No devices match the current filter.'
          : 'No scan devices. Click “Add Device” to register one.';
        tr.appendChild(td);
        tbody.appendChild(tr);
        return;
      }
      rows.forEach(({ d, i }) => tbody.appendChild(buildScanDeviceRow(daemon, domain, list, d, i)));
    }

    renderBody();
    return wrap;
  }

  function buildScanDeviceRow(daemon, domain, list, d, idx) {
    const tr = document.createElement('tr');
    if (!d.enabled) tr.classList.add('sd-row-disabled');

    // Enable/disable toggle
    const tdStatus = document.createElement('td');
    tdStatus.className = 'sd-col-status';
    const toggle = document.createElement('button');
    toggle.type = 'button';
    toggle.className = 'sd-toggle' + (d.enabled ? ' on' : '');
    toggle.setAttribute('role', 'switch');
    toggle.setAttribute('aria-checked', d.enabled ? 'true' : 'false');
    toggle.title = d.enabled ? 'Enabled — click to disable' : 'Disabled — click to enable';
    toggle.innerHTML = '<span class="sd-toggle-knob"></span>';
    toggle.addEventListener('click', () => {
      list[idx].enabled = !list[idx].enabled;
      persistScanDevices(daemon, domain, list);
    });
    tdStatus.appendChild(toggle);
    tr.appendChild(tdStatus);

    // IP
    const tdIp = document.createElement('td');
    tdIp.className = 'sd-col-ip st-td-ip';
    tdIp.textContent = d.ip || '(no ip)';
    tr.appendChild(tdIp);

    // Method badge
    const tdM = document.createElement('td');
    tdM.className = 'sd-col-method';
    tdM.appendChild(methodBadge(d.method));
    tr.appendChild(tdM);

    // Details — label/value pairs, e.g. "Community: public", "User: admin · Security: authPriv"
    const tdD = document.createElement('td');
    tdD.className = 'sd-details';
    METHODS[d.method].summary(d).forEach(([label, value], i) => {
      if (i > 0) tdD.appendChild(document.createTextNode(' · '));
      const lbl = document.createElement('span');
      lbl.className = 'sd-details-label';
      lbl.textContent = `${label}: `;
      tdD.appendChild(lbl);
      const val = document.createElement('span');
      val.className = 'sd-details-value';
      val.textContent = value;
      tdD.appendChild(val);
    });
    tr.appendChild(tdD);

    // Actions
    const tdA = document.createElement('td');
    tdA.className = 'sd-col-actions';
    const editB = sdIconBtn('edit', 'Edit', () => {
      openScanDeviceEditor({
        title: `Edit ${d.ip || 'Device'}`,
        device: d,
        existingIps: scanDeviceIps(list, idx),
        onSave: (dev) => { list[idx] = dev; persistScanDevices(daemon, domain, list); },
      });
    });
    const delB = sdIconBtn('trash', 'Remove', () => {
      list.splice(idx, 1);
      persistScanDevices(daemon, domain, list);
    });
    delB.classList.add('sd-action-danger');
    tdA.append(editB, delB);
    tr.appendChild(tdA);

    return tr;
  }

  function sdIconBtn(kind, label, onClick) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'sd-icon-btn';
    b.title = label;
    b.setAttribute('aria-label', label);
    const paths = {
      edit:  '<path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/>',
      trash: '<polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
    };
    b.innerHTML =
      `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" ` +
      `stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${paths[kind]}</svg>`;
    b.addEventListener('click', onClick);
    return b;
  }

  // Add/edit a single scan device. A method selector swaps the parameter fields
  // live; Save validates (required fields, IP format, duplicate IP) before
  // handing the normalized {ip, method, enabled, ...params} back to onSave.
  function openScanDeviceEditor({ title, device, existingIps, onSave }) {
    document.getElementById('sdEditorOverlay')?.remove();

    const isEdit = !!device;
    const draft = Object.assign(
      { ip: '', method: 'v2c', enabled: true },
      device ? JSON.parse(JSON.stringify(device)) : {});

    // Seed defaults for the active method; API values are typed in config but the
    // form inputs are strings, so stringify them for editing.
    function seedDefaults() {
      const defs = METHODS[draft.method].defaults;
      Object.keys(defs).forEach(k => { if (draft[k] === undefined) draft[k] = defs[k]; });
      if (draft.method === 'api') {
        draft.port       = String(draft.port ?? METHODS.api.defaults.port);
        draft.verify_tls = String(draft.verify_tls === true || draft.verify_tls === 'true');
      }
    }
    seedDefaults();

    const overlay = document.createElement('div');
    overlay.id = 'sdEditorOverlay';
    overlay.className = 'modal-overlay v3-editor-overlay sd-editor-overlay';
    const modal = document.createElement('div');
    modal.className = 'modal v3-editor-modal sd-editor-modal';

    // Slide back out before detaching, mirroring the .st-edit-panel close motion.
    function closeOverlay() {
      overlay.classList.remove('visible');
      overlay.addEventListener('transitionend', () => overlay.remove(), { once: true });
    }

    const header = document.createElement('div');
    header.className = 'modal-header';
    header.innerHTML =
      '<div class="modal-header-left"><svg width="15" height="15" viewBox="0 0 24 24" fill="none" ' +
      'stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">' +
      '<rect x="2" y="3" width="20" height="7" rx="2"/><rect x="2" y="14" width="20" height="7" rx="2"/>' +
      '<line x1="6" y1="6.5" x2="6.01" y2="6.5"/><line x1="6" y1="17.5" x2="6.01" y2="17.5"/></svg>' +
      `<span class="modal-title">${title}</span></div>`;
    const closeBtn = document.createElement('button');
    closeBtn.className = 'modal-close';
    closeBtn.setAttribute('aria-label', 'Close');
    closeBtn.innerHTML =
      '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" ' +
      'stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>';
    closeBtn.addEventListener('click', closeOverlay);
    header.appendChild(closeBtn);
    modal.appendChild(header);

    const body = document.createElement('div');
    body.className = 'modal-body v3-editor-body';
    modal.appendChild(body);

    function field(key, label, type, options) {
      const fw = document.createElement('div');
      fw.className = 'st-edit-field';
      const lbl = document.createElement('label');
      lbl.className = 'st-edit-field-label';
      lbl.textContent = label;
      fw.appendChild(lbl);

      // Preset dropdown (e.g. SNMP community public/private) with a "Custom…"
      // fallback for devices that don't use the convention default — the device's
      // actual community string can be anything, so we can't lock this to a fixed set.
      if (type === 'select-or-custom') {
        const CUSTOM = '__custom__';
        const isPreset = options.includes(draft[key]);
        const select = document.createElement('select');
        select.className = 'settings-field-input';
        select.dataset.fkey = key;
        options.forEach(o => {
          const op = document.createElement('option');
          op.value = o; op.textContent = o; select.appendChild(op);
        });
        const customOpt = document.createElement('option');
        customOpt.value = CUSTOM; customOpt.textContent = 'Custom…';
        select.appendChild(customOpt);
        select.value = isPreset ? draft[key] : CUSTOM;

        const customInput = document.createElement('input');
        customInput.type = 'password';
        customInput.className = 'settings-field-input settings-field-input-text sd-custom-input';
        customInput.placeholder = 'Enter custom value';
        customInput.dataset.fkey = key;
        customInput.style.display = isPreset ? 'none' : '';
        customInput.value = isPreset ? '' : (draft[key] || '');

        select.addEventListener('change', () => {
          if (select.value === CUSTOM) {
            customInput.style.display = '';
            customInput.value = '';
            draft[key] = '';
            customInput.focus();
          } else {
            customInput.style.display = 'none';
            draft[key] = select.value;
          }
          clearErr(key);
        });
        customInput.addEventListener('input', () => { draft[key] = customInput.value; clearErr(key); });

        fw.appendChild(select);
        fw.appendChild(customInput);
        const err = document.createElement('div');
        err.className = 'sd-field-err';
        err.dataset.errFor = key;
        fw.appendChild(err);
        return fw;
      }

      let input;
      if (type === 'select') {
        input = document.createElement('select');
        input.className = 'settings-field-input';
        options.forEach(o => {
          const op = document.createElement('option');
          op.value = o; op.textContent = o; input.appendChild(op);
        });
      } else {
        input = document.createElement('input');
        input.type = type === 'password' ? 'password' : 'text';
        input.className = 'settings-field-input settings-field-input-text';
      }
      input.value = draft[key] !== undefined ? draft[key] : '';
      input.dataset.fkey = key;
      input.addEventListener('input',  () => { draft[key] = input.value; clearErr(key); });
      input.addEventListener('change', () => { draft[key] = input.value; });
      fw.appendChild(input);
      const err = document.createElement('div');
      err.className = 'sd-field-err';
      err.dataset.errFor = key;
      fw.appendChild(err);
      return fw;
    }

    function clearErr(key) {
      body.querySelector(`[data-err-for="${key}"]`)?.replaceChildren();
      body.querySelector(`[data-fkey="${key}"]`)?.classList.remove('sd-input-err');
    }
    function showErrs(errs) {
      body.querySelectorAll('[data-err-for]').forEach(e => { e.textContent = ''; });
      body.querySelectorAll('.sd-input-err').forEach(i => i.classList.remove('sd-input-err'));
      Object.entries(errs).forEach(([k, msg]) => {
        const e   = body.querySelector(`[data-err-for="${k}"]`);
        const inp = body.querySelector(`[data-fkey="${k}"]`);
        if (e)   e.textContent = msg;
        if (inp) inp.classList.add('sd-input-err');
      });
    }

    function renderFields() {
      body.innerHTML = '';
      body.appendChild(field('ip', 'IP Address', 'text'));

      // Method segmented selector
      const mw = document.createElement('div');
      mw.className = 'st-edit-field';
      const mlbl = document.createElement('label');
      mlbl.className = 'st-edit-field-label';
      mlbl.textContent = 'Scan Method';
      mw.appendChild(mlbl);
      const seg = document.createElement('div');
      seg.className = 'sd-method-seg';
      METHOD_ORDER.forEach(m => {
        const opt = document.createElement('button');
        opt.type = 'button';
        opt.className = 'sd-method-opt' + (draft.method === m ? ' active' : '');
        opt.textContent = METHODS[m].label;
        opt.addEventListener('click', () => {
          if (draft.method === m) return;
          draft.method = m;
          seedDefaults();
          renderFields();
        });
        seg.appendChild(opt);
      });
      mw.appendChild(seg);
      body.appendChild(mw);

      // Method-specific fields
      METHODS[draft.method].fields.forEach(f => body.appendChild(field(f.key, f.label, f.type, f.options)));
    }
    renderFields();

    const footer = document.createElement('div');
    footer.className = 'modal-footer';
    footer.appendChild(document.createElement('span'));
    const actions = document.createElement('div');
    actions.className = 'modal-footer-actions';
    const cancel = document.createElement('button');
    cancel.type = 'button';
    cancel.className = 'btn btn-ghost';
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', closeOverlay);
    const save = document.createElement('button');
    save.type = 'button';
    save.className = 'btn btn-primary';
    save.textContent = isEdit ? 'Update' : 'Add';
    save.addEventListener('click', () => {
      draft.ip = (draft.ip || '').trim();
      const errs = validateScanDevice(draft, existingIps);
      if (Object.keys(errs).length) { showErrs(errs); return; }
      closeOverlay();
      const out = { ip: draft.ip, method: draft.method, enabled: draft.enabled !== false };
      METHODS[draft.method].fields.forEach(f => { out[f.key] = draft[f.key]; });
      onSave(out);
    });
    actions.append(cancel, save);
    footer.appendChild(actions);
    modal.appendChild(footer);

    overlay.appendChild(modal);
    overlay.addEventListener('click', e => { if (e.target === overlay) closeOverlay(); });
    document.body.appendChild(overlay);
    requestAnimationFrame(() => overlay.classList.add('visible'));
    setTimeout(() => body.querySelector('[data-fkey="ip"]')?.focus(), 0);
  }

  // ── Edit slide-over panel ─────────────────────────────────────────────────

  function openEditPanel(daemon, domain, values, panelTitle, keyFilter) {
    editContext = { daemon, domain, values };
    editMode = 'scalar';
    editDraft.clear();

    const panel    = document.getElementById('stEditPanel');
    const backdrop = document.getElementById('stEditBackdrop');
    const titleEl  = document.getElementById('stEditTitle');
    const body     = document.getElementById('stEditBody');
    if (!panel || !body) return;

    titleEl.textContent = panelTitle || `Edit — ${DOMAIN_LABELS[domain] || domain}`;
    body.innerHTML = '';

    const keys = keyFilter || Object.keys(values);

    keys.forEach(k => {
      if (values[k] !== undefined) body.appendChild(buildEditField(daemon, domain, k, values[k]));
    });

    // Restore already-staged values
    pending.forEach((change, sk) => {
      if (change.daemon === daemon && change.domain === domain) {
        editDraft.set(change.key, change.newValue);
        const input = body.querySelector(`[data-key="${change.key}"]`);
        if (input && input.type !== 'hidden') input.value = change.newValue;
      }
    });

    backdrop.classList.add('visible');
    panel.classList.add('visible');
  }

  function closeEditPanel() {
    document.getElementById('stEditPanel')?.classList.remove('visible');
    document.getElementById('stEditBackdrop')?.classList.remove('visible');
    editContext = null;
    editMode = 'scalar';
    rackEditIdx = null;
    rackAllocDraft = [];
    editDraft.clear();
  }

  function applyEditPanel() {
    if (editMode === 'rackNew')  { applyRackAdd();   return; }
    if (editMode === 'rackEdit') { applyRackAlloc(); return; }
    if (!editContext) return;
    const { daemon, domain, values } = editContext;

    editDraft.forEach((newVal, key) => {
      const sk  = stageKey(daemon, domain, key);
      const orig = String(values[key]);
      const nv   = String(newVal);
      if (nv === orig) {
        pending.delete(sk);
      } else {
        pending.set(sk, { daemon, domain, key, oldValue: values[key], newValue: newVal });
      }
    });

    refreshPendingBadge();
    // Re-render the specific card with pending changes merged into displayed values
    if (currentData) {
      const card = container?.querySelector(
        `.settings-domain-card[data-daemon="${cssEsc(daemon)}"][data-domain="${cssEsc(domain)}"]`);
      if (card) {
        const displayed = Object.assign({}, values);
        pending.forEach(change => {
          if (change.daemon === daemon && change.domain === domain) {
            displayed[change.key] = change.newValue;
          }
        });
        const fresh = buildDomainCard(daemon, domain, displayed);
        card.replaceWith(fresh);
      }
    }
    closeEditPanel();
  }

  // ── Edit field builders (used inside the slide-over panel) ────────────────

  function buildEditField(daemon, domain, key, value) {
    const isStr  = STRING_KEYS.has(key);
    const isList = LIST_KEYS.has(key);

    const wrap = document.createElement('div');
    wrap.className = 'st-edit-field';

    const lbl = document.createElement('label');
    lbl.className = 'st-edit-field-label';
    lbl.textContent = KEY_LABELS[key] || key;

    wrap.appendChild(lbl);

    if (isList) {
      // Tag display + Edit button (opens list editor)
      const tags = buildViewTags(value);
      tags.className = 'le-tags-wrap st-edit-tags';

      const hidden = document.createElement('input');
      hidden.type = 'hidden';
      hidden.dataset.key = key;
      hidden.value = String(value);

      function refreshTags(v) {
        const items = getItems(v);
        tags.innerHTML = '';
        if (items.length === 0) {
          tags.innerHTML = '<span class="le-tag-empty">None</span>';
        } else {
          items.forEach(item => {
            const t = document.createElement('span');
            t.className = 'le-tag';
            t.textContent = item;
            tags.appendChild(t);
          });
        }
      }

      const editListBtn = document.createElement('button');
      editListBtn.type = 'button';
      editListBtn.className = 'btn btn-ghost le-edit-btn';
      editListBtn.innerHTML =
        `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">` +
        `<path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/>` +
        `<path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg> Edit list`;

      editListBtn.addEventListener('click', () => {
        openListEditor({
          title:       KEY_LABELS[key] || key,
          hint:        KEY_HINT[key] || '',
          placeholder: KEY_PLACEHOLDER[key] || '',
          items:       getItems(hidden.value),
          onSave(newVal) {
            hidden.value = newVal;
            refreshTags(newVal);
            editDraft.set(key, newVal);
          },
        });
      });

      wrap.appendChild(tags);
      wrap.appendChild(editListBtn);
      wrap.appendChild(hidden);
      if (KEY_HINT[key]) {
        const hint = document.createElement('span');
        hint.className = 'st-edit-field-hint';
        hint.textContent = KEY_HINT[key];
        wrap.appendChild(hint);
      }
    } else {
      const input = document.createElement('input');
      input.type = isStr ? 'text' : 'number';
      input.className = 'settings-field-input' + (isStr ? ' settings-field-input-text' : '');
      input.dataset.key = key;
      input.value = String(value);
      if (KEY_PLACEHOLDER[key]) input.placeholder = KEY_PLACEHOLDER[key];

      input.addEventListener('input', () => {
        const v = isStr ? input.value : Number(input.value);
        editDraft.set(key, v);
      });

      wrap.appendChild(input);
    }

    return wrap;
  }

  // ── List editor modal ─────────────────────────────────────────────────────

  function openListEditor({ title, hint, placeholder, items, onSave }) {
    const overlay = document.getElementById('listEditorOverlay');
    const titleEl = document.getElementById('listEditorTitle');
    const hintEl  = document.getElementById('listEditorHint');
    const listEl  = document.getElementById('listEditorItems');
    if (!overlay) return;

    titleEl.textContent = title;
    hintEl.textContent  = hint || '';

    let current = [...items];

    function renderItems(arr) {
      listEl.innerHTML = '';
      if (arr.length === 0) {
        listEl.innerHTML = '<div class="le-empty">No entries. Add one below.</div>';
        return;
      }
      arr.forEach((val, idx) => {
        const row = document.createElement('div');
        row.className = 'le-item';
        row.innerHTML =
          `<span class="le-item-idx">${idx + 1}</span>` +
          `<span class="le-item-val">${val}</span>` +
          `<button class="le-item-del" data-idx="${idx}" title="Remove">` +
          `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">` +
          `<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button>`;
        listEl.appendChild(row);
      });
    }

    renderItems(current);

    listEl.onclick = (e) => {
      const btn = e.target.closest('.le-item-del');
      if (!btn) return;
      current.splice(Number(btn.dataset.idx), 1);
      renderItems(current);
    };

    const addInput = document.getElementById('listEditorInput');
    const addBtn   = document.getElementById('listEditorAddBtn');
    if (addInput) { addInput.value = ''; addInput.placeholder = placeholder || ''; }

    function addItem() {
      const val = (addInput?.value || '').trim();
      if (!val || current.includes(val)) return;
      current.push(val);
      renderItems(current);
      if (addInput) addInput.value = '';
      addInput?.focus();
    }

    addBtn && (addBtn.onclick = addItem);
    addInput && (addInput.onkeydown = e => { if (e.key === 'Enter') { e.preventDefault(); addItem(); } });

    listEditorCallback = () => onSave(current.join(','));
    overlay.classList.add('visible');
    addInput?.focus();
  }

  function closeListEditor(save) {
    const overlay = document.getElementById('listEditorOverlay');
    if (!overlay) return;
    if (save && listEditorCallback) listEditorCallback();
    listEditorCallback = null;
    overlay.classList.remove('visible');
  }

  // ── Tab init ──────────────────────────────────────────────────────────────

  function initTab() {
    const params = new URLSearchParams(window.location.search);
    let t = params.get('tab') || 'protocol';
    // Back-compat: old per-protocol tabs now live under the Protocol sub-tabs.
    if (PROTOCOLS[t]) { activeProto = t; t = 'protocol'; }
    activeTab = TABS[t] ? t : 'protocol';

    const p = params.get('proto');
    if (p && PROTOCOLS[p]) activeProto = p;
    if (!PROTOCOLS[activeProto]) activeProto = 'icmp';

    // Sidebar active state (incl. the Configuration flyout) is owned by main.js.
    updatePageHeading();
  }

  // Horizontal sub-tab bar (ICMP / SNMP / LLDP) at the top of the Protocol page.
  // Switching is client-side: it updates the URL (bookmarkable) and re-renders.
  function buildSubtabBar() {
    const bar = document.createElement('div');
    bar.className = 'st-subtabs';
    TABS.protocol.protocols.forEach(id => {
      const proto = PROTOCOLS[id];
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'st-subtab' + (id === activeProto ? ' active' : '');
      btn.textContent = proto.label;
      btn.addEventListener('click', () => {
        if (id === activeProto) return;
        activeProto = id;
        const u = new URL(window.location.href);
        u.searchParams.set('tab', 'protocol');
        u.searchParams.set('proto', id);
        window.history.replaceState({}, '', u);
        updatePageHeading();
        renderTabContent(currentData);
      });
      bar.appendChild(btn);
    });
    return bar;
  }

  // Title/subtitle reflect the active tab (and, on Protocol, the active sub-tab).
  function updatePageHeading() {
    const tab = TABS[activeTab];
    if (!tab) return;
    if (pageTitleEl) pageTitleEl.textContent = tab.title;
    if (pageSubtitleEl) {
      pageSubtitleEl.textContent = activeTab === 'protocol'
        ? (PROTOCOLS[activeProto]?.subtitle || tab.subtitle)
        : tab.subtitle;
    }
  }


  // ── Layout ────────────────────────────────────────────────────────────────

  // Render the domain cards for a given daemon/domain set into `container`.
  // Returns true if at least one card was rendered.
  function renderDomains(data, daemonList, domainList) {
    const daemons = (data && data.daemons) || {};
    let any = false;
    (daemonList || []).forEach(daemon => {
      const tuning = daemons[daemon] || {};
      (domainList || []).forEach(domain => {
        const values = tuning[domain];
        if (!values || typeof values !== 'object' || Object.keys(values).length === 0) return;
        container.appendChild(buildDomainCard(daemon, domain, values));
        any = true;
      });
    });
    return any;
  }

  function emptyState(msg) {
    const div = document.createElement('div');
    div.className = 'settings-empty';
    div.textContent = msg;
    container.appendChild(div);
  }

  // ── Rack List editor (Configuration ▸ Rack) ────────────────────────────────
  // Layout only: each rack is { name, description, type, devices:[] }. `type` is the
  // rack size (U height) chosen from RACK_TYPES; the Resource ▸ Rack Management page
  // lays out that many U slots and owns devices[] (preserved verbatim here).
  const RACK_TYPES = ['42U', '48U', '24U', '12U', '9U', '6U'];
  function rackEsc(s) {
    return String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function stageRacks(next) {
    setComplexPending('engined', 'rack', 'racks', originalComplex('engined', 'rack', 'racks'), next);
    refreshPendingBadge();
    renderRackList();
  }

  async function ensureRackDevices() {
    if (rackDevices.length) return;
    try {
      const resp = await fetchJSON('/api/devices');
      const d = resp ? await resp.json() : null;
      rackDevices = (d && d.devices) || [];
    } catch (_) { rackDevices = []; }
  }

  function rackPanel() {
    return {
      panel: document.getElementById('stEditPanel'),
      backdrop: document.getElementById('stEditBackdrop'),
      title: document.getElementById('stEditTitle'),
      body: document.getElementById('stEditBody'),
    };
  }
  function showRackPanel(p) { p.backdrop.classList.add('visible'); p.panel.classList.add('visible'); }

  // ── Add Rack (slide-over form: name + description + type) ───────────────────
  function openRackAdd() {
    editMode = 'rackNew';
    editContext = null; editDraft.clear(); rackEditIdx = null;
    const p = rackPanel();
    if (!p.panel || !p.body) return;
    p.title.textContent = 'Add Rack';
    p.body.innerHTML = `
      <div class="st-edit-field">
        <label class="st-edit-field-label">Name</label>
        <input type="text" class="settings-field-input" data-role="rkName" placeholder="Rack name…" />
      </div>
      <div class="st-edit-field">
        <label class="st-edit-field-label">Description</label>
        <input type="text" class="settings-field-input" data-role="rkDesc" placeholder="Optional…" />
      </div>
      <div class="st-edit-field">
        <label class="st-edit-field-label">Type</label>
        <select class="settings-field-input" data-role="rkType">
          ${RACK_TYPES.map(t => `<option value="${t}">${t}</option>`).join('')}
        </select>
      </div>`;
    showRackPanel(p);
    setTimeout(() => p.body.querySelector('[data-role="rkName"]')?.focus(), 0);
  }

  function applyRackAdd() {
    const body = document.getElementById('stEditBody');
    const name = body.querySelector('[data-role="rkName"]').value.trim();
    if (!name) { alert('Rack name is required.'); return; }
    const racks = effectiveArray('engined', 'rack', 'racks');
    if (racks.some(r => r.name === name)) { alert('A rack with that name already exists.'); return; }
    const desc = body.querySelector('[data-role="rkDesc"]').value.trim();
    const type = body.querySelector('[data-role="rkType"]').value;
    stageRacks(racks.concat([{ name, description: desc, type, devices: [] }]));
    closeEditPanel();
  }

  // ── Edit Rack allocation (slide-over: pick device → pick U range) ───────────
  async function openRackAlloc(idx) {
    const racks = effectiveArray('engined', 'rack', 'racks');
    const r = racks[idx];
    if (!r) return;
    rackEditIdx = idx;
    editMode = 'rackEdit';
    editContext = null; editDraft.clear();
    rackAllocDraft = JSON.parse(JSON.stringify(r.devices || []));   // working copy

    await ensureRackDevices();

    const p = rackPanel();
    if (!p.panel || !p.body) return;
    p.title.textContent = `Edit — ${r.name}`;
    renderRackAllocBody(r);
    showRackPanel(p);
  }

  function rackOccupied(draft) {
    // unit -> true for every U covered by a draft allocation (unit..unit+size-1).
    const occ = {};
    draft.forEach(d => { const s = d.size || 1; for (let i = 0; i < s; i++) occ[d.unit + i] = true; });
    return occ;
  }

  function renderRackAllocBody(r) {
    const body = document.getElementById('stEditBody');
    const height = parseInt(r.type, 10) || 42;
    const occ = rackOccupied(rackAllocDraft);

    const assignedIps = new Set(rackAllocDraft.map(d => d.ip));
    const devOpts = '<option value="">— select device —</option>' + rackDevices.slice()
      .filter(d => !assignedIps.has(d.primary_ip))
      .sort((a, b) => (a.hostname || a.primary_ip).localeCompare(b.hostname || b.primary_ip))
      .map(d => `<option value="${rackEsc(d.primary_ip)}" data-name="${rackEsc(d.hostname || d.primary_ip)}">${rackEsc(d.hostname || d.primary_ip)} (${rackEsc(d.primary_ip)})</option>`).join('');
    const uOpts = sel => Array.from({ length: height }, (_, i) => height - i)
      .map(n => `<option value="${n}"${n === sel ? ' selected' : ''}>U${n}</option>`).join('');

    const list = rackAllocDraft.length ? rackAllocDraft
      .slice().sort((a, b) => b.unit - a.unit)
      .map(d => {
        const end = d.unit + (d.size || 1) - 1;
        const range = d.size > 1 ? `U${d.unit}–U${end}` : `U${d.unit}`;
        return `<div class="rk-alloc-item">
          <span class="rk-alloc-range">${range}</span>
          <span class="rk-dev-name">${rackEsc(d.device || d.ip)}</span>
          <code class="rk-dev-ip">${rackEsc(d.ip)}</code>
          <button class="rack-del rk-alloc-del" data-ip="${rackEsc(d.ip)}" title="Remove">×</button>
        </div>`;
      }).join('') : '<div class="st-edit-field-hint">No devices allocated yet.</div>';

    const freeUnits = height - Object.keys(occ).length;
    body.innerHTML = `
      <div class="rk-edit-summary">
        <div class="rk-edit-rackname">${rackEsc(r.name)}</div>
        <div class="rk-edit-meta">${rackEsc(r.type || '42U')} · ${freeUnits}U free · ${rackAllocDraft.length} device(s)</div>
        ${r.description ? `<div class="rk-edit-desc">${rackEsc(r.description)}</div>` : ''}
      </div>

      <div class="rk-edit-section">
        <div class="rk-edit-section-title">Allocated devices</div>
        <div class="rk-alloc-list">${list}</div>
      </div>

      <div class="rk-edit-section">
        <div class="rk-edit-section-title">Add device</div>
        ${rackDevices.length ? `
          <label class="rk-field-label">Device</label>
          <select class="settings-field-input" data-role="aDev">${devOpts}</select>
          <label class="rk-field-label">Units</label>
          <div class="rk-range-row">
            <select class="settings-field-input rk-range-sel" data-role="aFrom">${uOpts(1)}</select>
            <span class="rk-range-sep">to</span>
            <select class="settings-field-input rk-range-sel" data-role="aTo">${uOpts(1)}</select>
            <button class="btn btn-sm btn-primary rk-add-btn" data-role="aAdd">Add</button>
          </div>
          <div class="rk-field-hint">A device can span multiple units (e.g. U5 → U8).</div>
        ` : '<div class="st-edit-field-hint">No devices discovered yet — run a probe/scan first.</div>'}
      </div>`;

    body.querySelectorAll('.rk-alloc-del').forEach(b => b.addEventListener('click', () => {
      rackAllocDraft = rackAllocDraft.filter(d => d.ip !== b.dataset.ip);
      renderRackAllocBody(r);
    }));
    const addBtn = body.querySelector('[data-role="aAdd"]');
    if (addBtn) addBtn.addEventListener('click', () => {
      const devSel = body.querySelector('[data-role="aDev"]');
      const ip = devSel.value;
      if (!ip) { alert('Select a device.'); return; }
      let from = parseInt(body.querySelector('[data-role="aFrom"]').value, 10);
      let to   = parseInt(body.querySelector('[data-role="aTo"]').value, 10);
      if (from > to) { const t = from; from = to; to = t; }
      const occNow = rackOccupied(rackAllocDraft);
      for (let n = from; n <= to; n++) {
        if (occNow[n]) { alert(`U${n} is already occupied.`); return; }
      }
      const opt = devSel.selectedOptions[0];
      rackAllocDraft.push({ device: (opt && opt.dataset.name) || ip, ip, unit: from, size: to - from + 1 });
      renderRackAllocBody(r);
    });
  }

  function applyRackAlloc() {
    const racks = effectiveArray('engined', 'rack', 'racks');
    if (rackEditIdx == null || !racks[rackEditIdx]) { closeEditPanel(); return; }
    racks[rackEditIdx] = Object.assign({}, racks[rackEditIdx], { devices: rackAllocDraft });
    stageRacks(racks);
    closeEditPanel();
  }

  // Read-only allocation summary shown when a rack row is expanded.
  function rackAllocSummary(r) {
    const devs = (r.devices || []).slice().sort((a, b) => b.unit - a.unit);
    if (!devs.length) return '<div class="rk-detail-empty">No devices allocated.</div>';
    return devs.map(d => {
      const size = d.size || 1;
      const range = size > 1 ? `U${d.unit}–U${d.unit + size - 1}` : `U${d.unit}`;
      return `<div class="rk-detail-item">
        <span class="rk-detail-range">${range}</span>
        <span class="rk-detail-name">${rackEsc(d.device || d.ip)}</span>
        <code class="rk-detail-ip">${rackEsc(d.ip)}</code>
      </div>`;
    }).join('');
  }

  function renderRackList() {
    container.innerHTML = '';
    const racks = effectiveArray('engined', 'rack', 'racks');

    const chevron = '<svg class="rk-chevron" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg>';

    const card = document.createElement('div');
    card.className = 'section-card rk-list-card';
    card.innerHTML = `
      <div class="section-header">
        <span class="section-title">Racks <span class="devices-count-badge">${racks.length}</span></span>
        <button class="btn btn-primary btn-sm" data-role="rkAdd">+ Add Rack</button>
      </div>
      <table class="device-table rk-table">
        <thead><tr>
          <th class="rk-col-exp"></th>
          <th>Name</th><th>Description</th><th>Type</th><th class="rk-col-dev">Devices</th>
          <th class="rk-col-act"></th>
        </tr></thead>
        <tbody>
          ${racks.length ? racks.map((r, i) => {
            const count = (r.devices || []).length;
            return `<tr class="rk-row" data-i="${i}">
              <td class="rk-col-exp"><button class="rk-exp-btn" data-i="${i}" aria-label="Show allocation">${chevron}</button></td>
              <td><strong>${rackEsc(r.name)}</strong></td>
              <td>${r.description ? rackEsc(r.description) : '<span class="muted">—</span>'}</td>
              <td class="muted">${rackEsc(r.type || '42U')}</td>
              <td class="rk-col-dev"><span class="rk-dev-count">${count}</span></td>
              <td class="rk-col-act">
                <button class="btn btn-ghost btn-sm rk-edit-btn" data-i="${i}">Edit</button>
                <button class="rk-del-btn" data-i="${i}" title="Delete rack" aria-label="Delete rack">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
                </button>
              </td>
            </tr>
            <tr class="rk-detail-row" data-i="${i}" hidden><td colspan="6">
              <div class="rk-detail">${rackAllocSummary(r)}</div>
            </td></tr>`;
          }).join('') : `<tr><td colspan="6" class="muted" style="padding:16px">No racks yet — click “Add Rack”.</td></tr>`}
        </tbody>
      </table>`;
    container.appendChild(card);

    card.querySelector('[data-role="rkAdd"]').addEventListener('click', openRackAdd);
    card.querySelectorAll('.rk-del-btn').forEach(b => b.addEventListener('click', () => {
      const i = parseInt(b.dataset.i, 10);
      if (confirm('Delete this rack? Its device allocation is removed too.'))
        stageRacks(racks.filter((_, idx) => idx !== i));
    }));
    card.querySelectorAll('.rk-edit-btn').forEach(b => b.addEventListener('click', () => {
      openRackAlloc(parseInt(b.dataset.i, 10));
    }));
    card.querySelectorAll('.rk-exp-btn').forEach(b => b.addEventListener('click', () => {
      const i = b.dataset.i;
      const detail = card.querySelector(`tr.rk-detail-row[data-i="${i}"]`);
      const open = detail.hasAttribute('hidden');
      if (open) detail.removeAttribute('hidden'); else detail.setAttribute('hidden', '');
      b.classList.toggle('expanded', open);
    }));
  }

  function renderTabContent(data) {
    container.innerHTML = '';
    const tab = TABS[activeTab];
    if (!tab) return;

    if (activeTab === 'protocol') {
      container.appendChild(buildSubtabBar());
      const proto = PROTOCOLS[activeProto];
      const any = renderDomains(data, proto.daemons, proto.domains);
      if (!any) emptyState(`${proto.label} configuration is not available yet.`);
      return;
    }

    // Rack List: layout (name + description) lives in running-config
    // (engined.service.rack.racks). Edits stage into `pending` like the other complex
    // arrays and commit through the normal Review/Commit flow. Device allocation is done
    // separately on the Resource ▸ Rack Management page.
    if (activeTab === 'rack') {
      renderRackList();
      return;
    }

    const any = renderDomains(data, tab.daemons, tab.domains);
    if (!any) emptyState('No configurable parameters for this section.');
  }

  function render(data) {
    currentData = data;
    renderTabContent(data);
  }

  // ── Pending badge ─────────────────────────────────────────────────────────

  function refreshPendingBadge() {
    const count = pending.size;
    if (commitPendingCount) commitPendingCount.textContent = String(count);
    commitPendingBadge?.classList.toggle('visible', count > 0);
    if (reviewBtn) reviewBtn.disabled = count === 0;
  }

  function discardAll() {
    if (pending.size === 0) return;
    pending.clear();
    if (currentData) renderTabContent(currentData);
    refreshPendingBadge();
    setStatus('All changes discarded.');
  }

  // ── Pending grouping ──────────────────────────────────────────────────────

  function groupPending() {
    const map = new Map();
    pending.forEach(c => {
      const k = `${c.daemon}::${c.domain}`;
      if (!map.has(k)) map.set(k, { daemon: c.daemon, domain: c.domain, changes: [] });
      map.get(k).changes.push(c);
    });
    return [...map.values()];
  }

  // ── JSON diff ─────────────────────────────────────────────────────────────

  function renderDiff(daemon, domain, changes) {
    diffViewer.innerHTML = '';
    if (diffPanelHint) diffPanelHint.textContent = `${daemon} · ${DOMAIN_LABELS[domain] || domain}`;

    const before  = Object.assign({}, currentData?.daemons?.[daemon]?.[domain] || {});
    const after   = Object.assign({}, before);
    changes.forEach(c => { before[c.key] = c.oldValue; after[c.key] = c.newValue; });
    const changed = new Set(changes.map(c => c.key));

    const block = document.createElement('div');
    block.className = 'diff-block';
    const hunk = document.createElement('div');
    hunk.className = 'diff-hunk';
    hunk.textContent = `@@ ${daemon}.${domain} @@`;
    block.appendChild(hunk);

    Object.keys(before).forEach((key, i, arr) => {
      const comma = i < arr.length - 1 ? ',' : '';
      if (changed.has(key)) {
        [['removed', before[key]], ['added', after[key]]].forEach(([type, val]) => {
          const row = document.createElement('div');
          row.className = `diff-line diff-line-${type}`;
          row.textContent = ` "${key}": ${JSON.stringify(val)}${comma}`;
          block.appendChild(row);
        });
      } else {
        const row = document.createElement('div');
        row.className = 'diff-line diff-line-ctx';
        row.textContent = ` "${key}": ${JSON.stringify(before[key])}${comma}`;
        block.appendChild(row);
      }
    });

    diffViewer.appendChild(block);
  }

  // ── Commit queue ──────────────────────────────────────────────────────────

  function iconSVG(type) {
    const icons = {
      pending: `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>`,
      running: `<svg class="spin" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>`,
      success: `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="20 6 9 17 4 12"/></svg>`,
      error:   `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>`,
    };
    return icons[type] || '';
  }

  let commitQueue = [];

  function buildCommitQueue() {
    commitQueue = groupPending().map(g => Object.assign(g, { status: 'pending' }));
  }

  function renderCommitQueueList() {
    commitQueueList.innerHTML = '';
    if (queueTotalBadge) queueTotalBadge.textContent = String(commitQueue.length);

    if (commitQueue.length === 0) {
      commitQueueList.innerHTML = '<div class="loading-row" style="padding:16px">No changes staged.</div>';
      return;
    }

    commitQueue.forEach((item, idx) => {
      const el = document.createElement('div');
      el.className = 'queue-item' + (selectedQueueKey === `${item.daemon}/${item.domain}` ? ' selected' : '');
      el.dataset.queueIdx = idx;

      const iconWrap = document.createElement('div');
      iconWrap.className = `queue-item-icon status-${item.status}`;
      iconWrap.innerHTML = iconSVG(item.status);

      const body = document.createElement('div');
      body.className = 'queue-item-body';
      body.innerHTML = `<div class="queue-item-name">${item.daemon}</div>
                        <div class="queue-item-sub">${DOMAIN_LABELS[item.domain] || item.domain}</div>`;

      const cnt = document.createElement('span');
      cnt.className = 'queue-item-count';
      cnt.textContent = item.changes.length;

      el.appendChild(iconWrap);
      el.appendChild(body);
      el.appendChild(cnt);
      el.addEventListener('click', () => {
        selectedQueueKey = `${item.daemon}/${item.domain}`;
        renderCommitQueueList();
        renderDiff(item.daemon, item.domain, item.changes);
      });

      commitQueueList.appendChild(el);
    });
  }

  function updateQueueItemStatus(idx, status) {
    commitQueue[idx].status = status;
    const el = commitQueueList.querySelector(`[data-queue-idx="${idx}"]`);
    if (!el) return;
    const iw = el.querySelector('.queue-item-icon');
    if (iw) { iw.className = `queue-item-icon status-${status}`; iw.innerHTML = iconSVG(status); }
  }

  // ── Commit modal ──────────────────────────────────────────────────────────

  function openReviewModal() {
    if (pending.size === 0) return;
    buildCommitQueue();
    selectedQueueKey = null;
    diffViewer.innerHTML = `<div class="diff-placeholder">
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" opacity=".3">
        <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
        <polyline points="14 2 14 8 20 8"/>
      </svg>
      <span>Select a queue item to preview the JSON diff</span>
    </div>`;
    if (diffPanelHint) diffPanelHint.textContent = 'Select a queue item';
    renderCommitQueueList();
    if (commitProgressFill) commitProgressFill.style.width = '0%';
    setCommitStatus('');
    commitBtn.disabled = false;
    reviewOverlay.classList.add('visible');
  }

  function closeReviewModal() {
    reviewOverlay.classList.remove('visible');
  }

  // ── Commit ────────────────────────────────────────────────────────────────

  const RELOAD_POLL_MS    = 500;
  const RELOAD_TIMEOUT_MS = 30000;

  function setProgress(pct) {
    if (commitProgressFill) commitProgressFill.style.width = `${pct}%`;
  }

  function commitFailed(msg) {
    commitQueue.forEach((_, i) => updateQueueItemStatus(i, 'error'));
    setCommitStatus(msg, 'error');
    setProgress(0);
    commitBtn.disabled  = false;
    discardBtn.disabled = false;
    reviewBtn.disabled  = pending.size === 0;
  }

  async function commitChanges() {
    if (commitQueue.length === 0) return;
    commitBtn.disabled  = true;
    discardBtn.disabled = true;
    reviewBtn.disabled  = true;
    commitQueue.forEach((_, i) => updateQueueItemStatus(i, 'running'));
    setProgress(0);
    setCommitStatus('Saving settings…');

    const changes = commitQueue.map(item => {
      const values = {};
      item.changes.forEach(c => { values[c.key] = c.newValue; });
      return { daemon: item.daemon, domain: item.domain, values };
    });

    let resp = null;
    try {
      const r = await fetchJSON('/api/settings/commit', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ changes }),
      });
      if (!r) return;
      resp = await r.json().catch(() => ({}));
      if (r.status === 409) { commitFailed('Another commit is already in progress. Please wait and retry.'); return; }
      if (!r.ok && !resp.results) { commitFailed(`Commit failed: ${resp.error || r.status}`); return; }
    } catch (e) {
      commitFailed(`Commit failed: ${e}`);
      return;
    }

    // Server now returns results:[] (engined owns persistence); treat applied>0 as full success.
    const serverApplied = resp.applied || 0;
    const serverFailed  = resp.failed  || 0;
    const results = resp.results || [];
    let applied = 0;

    commitQueue.forEach((item, i) => {
      const res = results.find(r => r.daemon === item.daemon && r.domain === item.domain);
      // Fall back to overall applied count when per-item results absent (engined owns persistence).
      const ok  = res ? res.status === 'ok' : serverFailed === 0 && serverApplied > 0;

      if (ok) {
        item.changes.forEach(c => {
          pending.delete(stageKey(c.daemon, c.domain, c.key));
          if (currentData?.daemons?.[item.daemon]?.[item.domain])
            currentData.daemons[item.daemon][item.domain][c.key] = c.newValue;
        });
        applied++;
      } else if (res) {
        updateQueueItemStatus(i, 'error');
      }
    });

    if (applied === 0 && (resp.failed || 0) > 0) {
      commitFailed('All changes failed to apply.');
      refreshPendingBadge();
      return;
    }

    setProgress(25);

    if (!resp.reloading) {
      commitQueue.forEach((_, i) => updateQueueItemStatus(i, 'success'));
      setProgress(100);
      setCommitStatus(`${applied} change${applied !== 1 ? 's' : ''} applied.`, 'success');
      discardBtn.disabled = false;
      reviewBtn.disabled  = pending.size === 0;
      refreshPendingBadge();
      if (currentData) renderTabContent(currentData);
      setTimeout(closeReviewModal, 1400);
      return;
    }

    setCommitStatus('Restarting services…');
    const pollStart = Date.now();

    await new Promise(resolve => {
      const timer = setInterval(async () => {
        const elapsed = Date.now() - pollStart;
        setProgress(25 + 65 * Math.min(elapsed / RELOAD_TIMEOUT_MS, 1));

        if (elapsed >= RELOAD_TIMEOUT_MS) {
          clearInterval(timer);
          commitFailed('Timeout — check daemon logs for details.');
          resolve();
          return;
        }

        try {
          const r = await fetchJSON('/api/settings/reload-status');
          if (!r) { clearInterval(timer); resolve(); return; }
          const s = await r.json().catch(() => ({}));
          if (s.status === 'complete') { clearInterval(timer); resolve(); }
        } catch (_) { /* keep polling */ }
      }, RELOAD_POLL_MS);
    });

    if (Date.now() - pollStart >= RELOAD_TIMEOUT_MS) return;

    commitQueue.forEach((item, i) => {
      const res = results.find(r => r.daemon === item.daemon && r.domain === item.domain);
      updateQueueItemStatus(i, (!res || res.status === 'ok') ? 'success' : 'error');
    });

    setProgress(100);
    setCommitStatus(`${applied} change${applied !== 1 ? 's' : ''} deployed — services restarted.`, 'success');
    setStatus(`${applied} change${applied !== 1 ? 's' : ''} deployed.`, 'success');
    discardBtn.disabled = false;
    reviewBtn.disabled  = pending.size === 0;
    refreshPendingBadge();
    if (currentData) renderTabContent(currentData);
    startTaskQueuePolling();
    setTimeout(closeReviewModal, 1200);
  }

  // ── Commit task-queue status bar ─────────────────────────────────────────

  function renderTaskQueueBar(tasks) {
    if (!taskQueueBar) return;

    const active = tasks.filter(t => t.status === 'pending' || t.status === 'running');
    if (active.length === 0) {
      taskQueueBar.classList.remove('visible');
      return;
    }

    taskQueueBar.classList.add('visible');
    taskQueueBar.innerHTML = '';

    active.forEach(t => {
      const chip = document.createElement('div');
      chip.className = `tq-chip tq-chip--${t.status}`;

      const dot = document.createElement('span');
      dot.className = 'tq-dot';

      const label = document.createElement('span');
      label.textContent = t.status === 'running'
        ? `Task #${t.id}  Running…`
        : `Task #${t.id}  Pending`;

      chip.appendChild(dot);
      chip.appendChild(label);
      taskQueueBar.appendChild(chip);
    });
  }

  async function pollTaskQueue() {
    try {
      const r = await fetchJSON('/api/settings/commit-queue');
      if (!r) return;
      const tasks = await r.json().catch(() => []);
      renderTaskQueueBar(Array.isArray(tasks) ? tasks : []);

      const anyActive = tasks.some(t => t.status === 'pending' || t.status === 'running');
      if (!anyActive) {
        clearInterval(taskQueuePollTimer);
        taskQueuePollTimer = null;
        // Refresh settings data once queue drains so values are up-to-date.
        const dr = await fetchJSON('/api/settings');
        if (dr && dr.ok) { const d = await dr.json(); render(d); }
      }
    } catch (_) { /* keep polling */ }
  }

  function startTaskQueuePolling() {
    if (taskQueuePollTimer) return;
    taskQueuePollTimer = setInterval(pollTaskQueue, 1000);
    pollTaskQueue();
  }

  // ── Load ──────────────────────────────────────────────────────────────────

  async function load() {
    setStatus('Loading…');
    try {
      const r = await fetchJSON('/api/settings');
      if (!r) return;
      if (!r.ok) { setStatus(`Failed to load settings (${r.status})`, 'error'); return; }
      const data = await r.json();
      pending.clear();
      render(data);
      refreshPendingBadge();
      setStatus('');
    } catch (e) {
      setStatus(`Failed to load settings: ${e}`, 'error');
    }
  }

  // ── Init ──────────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    container          = document.getElementById('settingsGroups');
    statusEl           = document.getElementById('settingsStatus');
    pageTitleEl        = document.getElementById('pageTitle');
    pageSubtitleEl     = document.getElementById('pageSubtitle');
    commitPendingBadge = document.getElementById('commitPendingBadge');
    commitPendingCount = document.getElementById('commitPendingCount');
    discardBtn         = document.getElementById('discardBtn');
    reviewBtn          = document.getElementById('reviewBtn');
    reviewOverlay      = document.getElementById('reviewOverlay');
    reviewCloseBtn     = document.getElementById('reviewCloseBtn');
    reviewCancelBtn    = document.getElementById('reviewCancelBtn');
    commitBtn          = document.getElementById('commitBtn');
    commitStatus       = document.getElementById('commitStatus');
    commitProgressFill = document.getElementById('commitProgressFill');
    commitQueueList    = document.getElementById('commitQueueList');
    queueTotalBadge    = document.getElementById('queueTotalBadge');
    diffViewer         = document.getElementById('diffViewer');
    diffPanelHint      = document.getElementById('diffPanelHint');
    taskQueueBar       = document.getElementById('taskQueueBar');

    if (!container) return;
    initTab();

    // List editor
    document.getElementById('listEditorSaveBtn')?.addEventListener('click', () => closeListEditor(true));
    document.getElementById('listEditorCancelBtn')?.addEventListener('click', () => closeListEditor(false));
    document.getElementById('listEditorCloseBtn')?.addEventListener('click', () => closeListEditor(false));
    document.getElementById('listEditorOverlay')?.addEventListener('click', e => {
      if (e.target === document.getElementById('listEditorOverlay')) closeListEditor(false);
    });

    // Edit panel
    document.getElementById('stEditCloseBtn')?.addEventListener('click', closeEditPanel);
    document.getElementById('stEditCancelBtn')?.addEventListener('click', closeEditPanel);
    document.getElementById('stEditApplyBtn')?.addEventListener('click', applyEditPanel);
    document.getElementById('stEditBackdrop')?.addEventListener('click', closeEditPanel);
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape') {
        if (document.getElementById('stEditPanel')?.classList.contains('visible')) closeEditPanel();
        else if (reviewOverlay?.classList.contains('visible')) closeReviewModal();
      }
    });

    // Commit flow
    discardBtn?.addEventListener('click', discardAll);
    reviewBtn?.addEventListener('click', openReviewModal);
    reviewCancelBtn?.addEventListener('click', closeReviewModal);
    reviewCloseBtn?.addEventListener('click', closeReviewModal);
    reviewOverlay?.addEventListener('click', e => { if (e.target === reviewOverlay) closeReviewModal(); });
    commitBtn?.addEventListener('click', commitChanges);

    load();
  });
})();
