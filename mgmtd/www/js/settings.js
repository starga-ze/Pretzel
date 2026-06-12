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
      subtitle: 'SNMP polling parameters, community string, and SNMPv3 credentials.',
      daemons:  ['snmpd'],
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
    // SNMP (snmpd/scan)
    community:                  'Community String',
    port:                       'Port',
    timeout_sec:                'Timeout (s)',
    retries:                    'Retries',
    max_concurrent:             'Max Concurrent',
    v2c_probe_timeout_ms:       'v2c Probe Timeout (ms)',
    v2c_probe_retries:          'v2c Probe Retries',
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
    community:                  'SNMP v2c community string used for polling.',
    port:                       'UDP port for SNMP requests.',
    timeout_sec:                'Per-request response timeout.',
    retries:                    'Retry attempts per request after a timeout.',
    max_concurrent:             'Maximum simultaneous SNMP sessions.',
    v2c_probe_timeout_ms:       'Initial v2c reachability probe timeout.',
    v2c_probe_retries:          'Retries for the initial v2c reachability probe.',
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

  // SNMPv3 fallback credentials (snmpd/scan). `v3` is the global default; `v3_devices`
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

  // ── State ─────────────────────────────────────────────────────────────────

  let currentData      = null;
  let activeTab        = 'protocol';
  let activeProto      = 'icmp';     // active sub-tab when activeTab === 'protocol'
  let selectedQueueKey = null;

  // staged edits: `${daemon} ${domain} ${key}` → { daemon, domain, key, oldValue, newValue }
  const pending = new Map();

  // edit panel state
  let editContext = null;   // { daemon, domain, values }
  let editMode    = 'scalar';   // 'scalar' (key fields) | 'v3' (SNMPv3 device list)
  let v3Working   = null;       // working copy of v3_devices while the v3 panel is open
  // in-panel staged values (before Apply)
  const editDraft = new Map();  // key → value

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
      // Scalar settings table. Nested SNMPv3 keys (v3 / v3_devices) are excluded here
      // and rendered by their own dedicated table below.
      const scalarKeys = keys.filter(k => k !== V3_OBJECT_KEY && k !== V3_DEVICES_KEY);
      card.appendChild(buildSettingsTable(
        DOMAIN_LABELS[domain] || domain, scalarKeys, values,
        () => openEditPanel(daemon, domain, values, null, scalarKeys),
        daemon, domain));

      // SNMPv3 fallback is per-IP only: a device listed here is retried with v3 on a
      // v2c timeout; everything else ends at v2c. Always shown so operators can add.
      if (domain === 'scan') {
        card.appendChild(buildV3DevicesTable(daemon, domain, values));
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

  // Stage (or clear) a complex object/array change and re-render the card.
  function stageComplex(daemon, domain, key, oldValue, newValue) {
    const sk = stageKey(daemon, domain, key);
    if (JSON.stringify(oldValue ?? null) === JSON.stringify(newValue ?? null)) {
      pending.delete(sk);
    } else {
      pending.set(sk, { daemon, domain, key, oldValue, newValue });
    }
    refreshPendingBadge();

    if (currentData && container) {
      const base = Object.assign({}, currentData?.daemons?.[daemon]?.[domain] || {});
      pending.forEach(c => {
        if (c.daemon === daemon && c.domain === domain) base[c.key] = c.newValue;
      });
      const card = container.querySelector(
        `.settings-domain-card[data-daemon="${cssEsc(daemon)}"][data-domain="${cssEsc(domain)}"]`);
      if (card) card.replaceWith(buildDomainCard(daemon, domain, base));
    }
  }

  // A credential cell shows the protocol plus a masked indicator — never the actual
  // password. "—" when the security level doesn't use that layer or no password is set.
  function credCell(used, proto, pw) {
    if (!used) return '—';
    return `${proto || '—'} · ${pw ? '••••••' : '—'}`;
  }

  // View-only SNMPv3 table. Editing happens in the right slide-over (openV3Panel),
  // opened by the standard pencil button — consistent with the other settings cards.
  function buildV3DevicesTable(daemon, domain, values) {
    const devices = Array.isArray(values[V3_DEVICES_KEY]) ? values[V3_DEVICES_KEY] : [];

    const wrap = document.createElement('div');
    wrap.className = 'st-table-card';

    const head = document.createElement('div');
    head.className = 'st-table-head';
    const t = document.createElement('span');
    t.className = 'st-table-title';
    t.textContent = 'SNMPv3';
    head.appendChild(t);
    head.appendChild(buildEditIconBtn(() => openV3Panel(daemon, domain, values)));
    wrap.appendChild(head);

    const table = document.createElement('table');
    table.className = 'st-table st-table-v3';
    table.innerHTML =
      '<thead><tr>' +
      '<th>IP Address</th><th>User</th><th>Security Level</th>' +
      '<th>Auth</th><th>Priv</th>' +
      '</tr></thead>';

    const tbody = document.createElement('tbody');

    if (!devices.length) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.colSpan = 5;
      td.className = 'st-td-empty';
      td.textContent = 'No per-IP overrides. The default credentials apply to all devices.';
      tr.appendChild(td);
      tbody.appendChild(tr);
    } else {
      devices.forEach(d => {
        const lvl = d.security_level || 'authPriv';
        const tr = document.createElement('tr');
        const mk = (txt, cls) => {
          const td = document.createElement('td');
          if (cls) td.className = cls;
          td.textContent = txt;
          return td;
        };
        tr.appendChild(mk(d.ip || '(no ip)', 'st-td-ip'));
        tr.appendChild(mk(d.user || '—'));
        tr.appendChild(mk(lvl));
        tr.appendChild(mk(credCell(lvl !== 'noAuthNoPriv', d.auth_protocol, d.auth_password), 'st-cred-muted'));
        tr.appendChild(mk(credCell(lvl === 'authPriv', d.priv_protocol, d.priv_password), 'st-cred-muted'));
        tbody.appendChild(tr);
      });
    }

    table.appendChild(tbody);
    wrap.appendChild(table);
    return wrap;
  }

  // ── SNMPv3 device-list editor (hosted in the right slide-over) ─────────────
  // The pencil opens the slide-over with the device list; per-device field entry
  // reuses openV3Editor. Changes accumulate in v3Working and are staged on Apply.

  function openV3Panel(daemon, domain, values) {
    editContext = { daemon, domain, values };
    editMode = 'v3';

    // Seed the working copy from any staged change, else the live value.
    const staged = pending.get(stageKey(daemon, domain, V3_DEVICES_KEY))?.newValue;
    const src = staged ?? values[V3_DEVICES_KEY] ?? [];
    v3Working = JSON.parse(JSON.stringify(Array.isArray(src) ? src : []));

    const panel    = document.getElementById('stEditPanel');
    const backdrop = document.getElementById('stEditBackdrop');
    const titleEl  = document.getElementById('stEditTitle');
    if (!panel) return;
    titleEl.textContent = 'Edit — SNMPv3';
    renderV3PanelBody();
    backdrop.classList.add('visible');
    panel.classList.add('visible');
  }

  function renderV3PanelBody() {
    const body = document.getElementById('stEditBody');
    if (!body) return;
    body.innerHTML = '';

    const addBtn = document.createElement('button');
    addBtn.type = 'button';
    addBtn.className = 'btn btn-ghost st-v3-add';
    addBtn.textContent = '+ Add device';
    addBtn.addEventListener('click', () => {
      openV3Editor({
        title: 'Add SNMPv3 Device',
        creds: Object.assign({ ip: '' }, V3_DEFAULTS),
        showIp: true,
        onSave: (c) => {
          if (!c.ip) { setStatus('Device IP is required.', 'error'); return; }
          v3Working.push(c);
          renderV3PanelBody();
        },
      });
    });
    body.appendChild(addBtn);

    if (!v3Working.length) {
      const empty = document.createElement('div');
      empty.className = 'st-edit-field-hint';
      empty.style.padding = '10px 2px';
      empty.textContent = 'No per-IP overrides. The default credentials apply to all devices.';
      body.appendChild(empty);
      return;
    }

    v3Working.forEach((d, i) => {
      const row = document.createElement('div');
      row.className = 'st-v3-row';

      const info = document.createElement('div');
      info.className = 'st-v3-row-info';
      const ip = document.createElement('span');
      ip.className = 'st-v3-row-ip';
      ip.textContent = d.ip || '(no ip)';
      const sub = document.createElement('span');
      sub.className = 'st-v3-row-sub';
      sub.textContent = `${d.user || '(no user)'} · ${d.security_level || 'authPriv'}`;
      info.append(ip, sub);

      const actions = document.createElement('div');
      actions.className = 'st-v3-row-actions';
      const editB = document.createElement('button');
      editB.type = 'button';
      editB.className = 'btn btn-ghost st-row-btn';
      editB.textContent = 'Edit';
      editB.addEventListener('click', () => {
        openV3Editor({
          title: `Edit ${d.ip || 'Device'}`,
          creds: Object.assign({ ip: '' }, V3_DEFAULTS, d),
          showIp: true,
          onSave: (c) => {
            if (!c.ip) { setStatus('Device IP is required.', 'error'); return; }
            v3Working[i] = c;
            renderV3PanelBody();
          },
        });
      });
      const delB = document.createElement('button');
      delB.type = 'button';
      delB.className = 'btn btn-ghost st-row-btn st-row-btn-danger';
      delB.textContent = 'Remove';
      delB.addEventListener('click', () => { v3Working.splice(i, 1); renderV3PanelBody(); });
      actions.append(editB, delB);

      row.append(info, actions);
      body.appendChild(row);
    });
  }

  function applyV3Panel() {
    if (!editContext) return;
    const { daemon, domain } = editContext;
    stageComplex(daemon, domain, V3_DEVICES_KEY,
                 originalComplex(daemon, domain, V3_DEVICES_KEY) || [], v3Working);
    closeEditPanel();
  }

  // Per-device field editor. Reuses the app's light .modal styling for consistency
  // (it sits above the slide-over, so it needs a higher z-index than the panel).
  function openV3Editor({ title, creds, showIp, onSave }) {
    document.getElementById('v3EditorOverlay')?.remove();

    const overlay = document.createElement('div');
    overlay.id = 'v3EditorOverlay';
    overlay.className = 'modal-overlay v3-editor-overlay visible';

    const modal = document.createElement('div');
    modal.className = 'modal v3-editor-modal';

    const header = document.createElement('div');
    header.className = 'modal-header';
    header.innerHTML =
      `<div class="modal-header-left">` +
      `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round">` +
      `<path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/>` +
      `<path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>` +
      `<span class="modal-title">${title}</span></div>`;
    const closeBtn = document.createElement('button');
    closeBtn.className = 'modal-close';
    closeBtn.setAttribute('aria-label', 'Close');
    closeBtn.innerHTML =
      `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">` +
      `<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>`;
    closeBtn.addEventListener('click', () => overlay.remove());
    header.appendChild(closeBtn);
    modal.appendChild(header);

    const body = document.createElement('div');
    body.className = 'modal-body v3-editor-body';

    const inputs = {};
    const fields = (showIp ? [{ key: 'ip', label: 'IP Address', type: 'text' }] : []).concat(V3_FIELDS);
    fields.forEach(f => {
      const wrap = document.createElement('div');
      wrap.className = 'st-edit-field';
      const lbl = document.createElement('label');
      lbl.className = 'st-edit-field-label';
      lbl.textContent = f.label;
      wrap.appendChild(lbl);

      let input;
      if (f.type === 'select') {
        input = document.createElement('select');
        input.className = 'settings-field-input';
        f.options.forEach(o => {
          const opt = document.createElement('option');
          opt.value = o; opt.textContent = o;
          input.appendChild(opt);
        });
      } else {
        input = document.createElement('input');
        input.type = f.type === 'password' ? 'password' : 'text';
        input.className = 'settings-field-input settings-field-input-text';
      }
      input.value = creds[f.key] !== undefined ? creds[f.key] : '';
      inputs[f.key] = input;
      wrap.appendChild(input);
      body.appendChild(wrap);
    });
    modal.appendChild(body);

    const footer = document.createElement('div');
    footer.className = 'modal-footer';
    footer.appendChild(document.createElement('span'));
    const actions = document.createElement('div');
    actions.className = 'modal-footer-actions';
    const cancel = document.createElement('button');
    cancel.type = 'button';
    cancel.className = 'btn btn-ghost';
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', () => overlay.remove());
    const save = document.createElement('button');
    save.type = 'button';
    save.className = 'btn btn-primary';
    save.textContent = 'Save';
    save.addEventListener('click', () => {
      const out = {};
      if (showIp) out.ip = (inputs.ip.value || '').trim();
      V3_FIELDS.forEach(f => { out[f.key] = inputs[f.key].value; });
      overlay.remove();
      onSave(out);
    });
    actions.append(cancel, save);
    footer.appendChild(actions);
    modal.appendChild(footer);

    overlay.appendChild(modal);
    overlay.addEventListener('click', e => { if (e.target === overlay) overlay.remove(); });
    document.body.appendChild(overlay);
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
    v3Working = null;
    editDraft.clear();
  }

  function applyEditPanel() {
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
    document.getElementById('stEditApplyBtn')?.addEventListener('click', () => {
      if (editMode === 'v3') applyV3Panel(); else applyEditPanel();
    });
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
