/* api-keys.js — Configuration ▸ API Profile ▸ API Key.
 *
 * An API Key is the credential pretzel uses against one device. It is bound to a Device rather
 * than floating free, because a PAN-OS key is issued by a specific box and is worthless on any
 * other — so "one credential, many devices" would be a fiction. The device also decides the
 * credential shape (NGFW: username/password today; Prisma Access comes later).
 *
 * The operator supplies the key-generation endpoint, since customer estates run releases we do
 * not control. Testing happens from the list, not the editor: a key is a thing you keep and
 * re-verify, so the row carries its own Test action and Status.
 *
 * Config vs state: the record below is what the operator declared — name, device, endpoint,
 * account. Everything the system produces about the key (the issued secret, when it expires,
 * how the last test went) is runtime state and is kept apart, because writing it into
 * running_config would mint a configuration version every time a key was re-issued and would
 * show machine noise in the operator's review diff. The database has api_key_state for exactly
 * this; until the encrypted credential store lands the same fields live per browser tab, which
 * is also why mgmtd refuses a commit carrying password/secret/api_key.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // Read live (not once): settings tabs switch client-side without a page load (see main.js).
  const activeTab = () => new URLSearchParams(location.search).get('tab') || 'sites';
  const DRAFT_KEY = 'api_keys';
  const SECRET_KEY = 'api_key_secrets';

  const { esc, newUuid } = window.NMS.utils;

  // Credential shape per device type. Prisma Access uses a different scheme (client id/secret
  // against a tenant), so it is declared but left unimplemented rather than faked with id/pw.
  const CREDENTIALS = {
    ngfw: {
      supported: true,
      keygenHint: '/api/?type=keygen',
      fields: [
        ['username', 'Username', 'text', 'svc-pretzel'],
        ['password', 'Password', 'password', ''],
      ],
    },
    prisma_access: {
      supported: false,
      keygenHint: '',
      fields: [],
      note: 'Prisma Access uses tenant OAuth credentials — not implemented yet.',
    },
  };
  const credSpec = (deviceType) => CREDENTIALS[deviceType] || CREDENTIALS.ngfw;

  const state = { keys: [] };
  let deployed = [];
  let editIdx = null;
  let draftOid = null;
  let draftSite = '';   // Editor-only: the site whose devices the Device select is scoped to.

  // ── Secrets (browser-only, see the header note) ──────────────────────────────
  // Runtime state for one key: { password, key, issued_at, expires_at, last_test }.
  // The destination is the api_key_state table; this is its stand-in until then.
  const secrets = {
    for: (oid) => (window.NMS.draft.get(SECRET_KEY, {})[oid] || {}),
    put(oid, vals) {
      const s = window.NMS.draft.get(SECRET_KEY, {});
      s[oid] = Object.assign(s[oid] || {}, vals);
      window.NMS.draft.set(SECRET_KEY, s);
    },
    drop(oid) {
      const s = window.NMS.draft.get(SECRET_KEY, {});
      delete s[oid];
      window.NMS.draft.set(SECRET_KEY, s);
    },
  };
  window.NMS.apiKeySecrets = secrets;

  function normalize(k) {
    return {
      // `uuid`/`id` = legacy keys from before the identifier merge.
      oid: (typeof k.oid === 'string' && k.oid) ? k.oid : (k.uuid || k.id || newUuid()),
      name: k.name || '',
      device: k.device || '',        // Device oid — decides the credential shape
      endpoint: k.endpoint || '',    // key-generation path, operator-entered
      username: k.username || '',
    };
  }

  const blank = () => normalize({ endpoint: CREDENTIALS.ngfw.keygenHint });

  // ── Staging ──────────────────────────────────────────────────────────────────
  const stage = () => { window.NMS.draft.set(DRAFT_KEY, state.keys); refreshPending(); };
  const refreshPending = () => window.NMS.staging.refresh();

  // ── Data load ────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const r = await fetch('/api/settings', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();
      window.NMS.draft.checkBase(d.version);
      const api = ((d.daemons || {}).scand || {}).api || {};
      deployed = (Array.isArray(api.api_keys) ? api.api_keys : []).map(normalize);
    } catch (_) { deployed = []; }
    const staged = window.NMS.draft.get(DRAFT_KEY, null);
    state.keys = Array.isArray(staged) ? staged.map(normalize) : JSON.parse(JSON.stringify(deployed));
    refreshPending();
  }

  const commitPayload = () => [{ daemon: 'scand', domain: 'api', values: { api_keys: state.keys } }];

  window.NMS.staging.register({
    key: DRAFT_KEY,
    dirty: () => JSON.stringify(state.keys) !== JSON.stringify(deployed),
    payload: commitPayload,
    before: () => ({ api_keys: deployed }),
    after: () => ({ api_keys: state.keys }),
    onPublished() {
      deployed = JSON.parse(JSON.stringify(state.keys));
      window.NMS.draft.clear(DRAFT_KEY);
    },
    problems() {
      const devices = (window.NMS.devices && window.NMS.devices.list()) || [];
      return state.keys
        .filter(k => !k.device || !devices.some(d => d.oid === k.device))
        .map(k => `API Key "${k.name}" is bound to a device that does not exist.`);
    },
  });

  // ── Cross-module surface ─────────────────────────────────────────────────────
  window.NMS.apiKeys = {
    list: () => state.keys.map(k => ({ oid: k.oid, name: k.name, device: k.device })),
    byOid: (oid) => state.keys.find(k => k.oid === oid) || null,
    label: (oid) => {
      const k = state.keys.find(x => x.oid === oid);
      return k ? k.name : null;
    },
    // The key is held by the appliance, not the browser; callers that need a call made with it
    // ask mgmtd to make it.
    hasKey: (oid) => !!secrets.for(oid).stored,
  };

  // ── Render ───────────────────────────────────────────────────────────────────
  const deviceOf = (k) => (window.NMS.devices && window.NMS.devices.byOid(k.device)) || null;
  const devices = () => (window.NMS.devices && window.NMS.devices.list()) || [];
  const sites = () => (window.NMS.sites && window.NMS.sites.list()) || [];
  const siteName = (oid) => (window.NMS.sites && window.NMS.sites.label(oid)) || '';

  // Devices are picked Site-first: choose a site, then only that site's devices are offered.
  function deviceOptsForSite(siteOid, selectedOid) {
    if (!siteOid) return '<option value="">— select a site first —</option>';
    const inSite = devices().filter(d => d.site === siteOid);
    if (!inSite.length) return '<option value="">— no devices in this site —</option>';
    return ['<option value="">— select a device —</option>'].concat(
      inSite.map(d => `<option value="${esc(d.oid)}" ${d.oid === selectedOid ? 'selected' : ''}>${
        esc(d.name || d.target)} (${esc(window.NMS.devices.typeLabel(d.device_type))})</option>`)
    ).join('');
  }

  function deviceCell(k) {
    const d = deviceOf(k);
    if (!d) return `<span class="ref-missing" title="Device ${esc(k.device)} no longer exists">missing</span>`;
    return `<div class="cell-name">${esc(d.name) || esc(d.target)}</div>
            <div class="cell-sub">${esc(window.NMS.devices.typeLabel(d.device_type))} · ${esc(d.target)}</div>`;
  }

  // The site is derived from the bound device, not stored on the key.
  function siteCell(k) {
    const d = deviceOf(k);
    const name = d ? siteName(d.site) : '';
    return name ? `<div class="cell-name">${esc(name)}</div>` : `<span class="muted">—</span>`;
  }

  // The key itself never comes back to the browser — it is encrypted and stored by engined, so
  // there is nothing here to mask. The row reports whether one is held.
  function keyCell(k) {
    const st = secrets.for(k.oid);
    if (!st.stored) return `<span class="muted">not generated</span>`;
    return `<code class="key-mask" title="Encrypted on the appliance; never sent to the browser">stored</code>`;
  }

  function expiryCell(k) {
    const st = secrets.for(k.oid);
    if (!st.stored) return `<span class="muted">—</span>`;
    if (!st.expires_at) {
      // PAN-OS keys do not expire unless an API key lifetime is configured on the device.
      return `<span class="st-never" title="No API key lifetime set on the device">no expiry</span>`;
    }
    const when = new Date(st.expires_at);
    const left = when.getTime() - Date.now();
    if (left <= 0) return `<span class="st-fail" title="${esc(when.toLocaleString())}">expired</span>`;
    const days = Math.floor(left / 86400000);
    return `<span class="${days <= 7 ? 'st-warn' : 'st-ok'}" title="${esc(when.toLocaleString())}">${
      days > 0 ? days + 'd left' : 'today'}</span>`;
  }

  function statusCell(k) {
    const t = secrets.for(k.oid).last_test;
    if (!t) return `<span class="st-never">never tested</span>`;
    const when = new Date(t.at).toLocaleString();
    return t.ok
      ? `<span class="st-ok" title="${esc(when)}">OK</span>`
      : `<span class="st-fail" title="${esc(t.detail || '')} — ${esc(when)}">failed</span>`;
  }

  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'api-key') return;

    const devices = (window.NMS.devices && window.NMS.devices.list()) || [];

    const rows = state.keys.length
      ? state.keys.map((k, i) => `
        <tr>
          <td class="col-name"><div class="cell-name">${esc(k.name) || '<span class="muted">unnamed</span>'}</div></td>
          <td class="col-site">${siteCell(k)}</td>
          <td class="col-device">${deviceCell(k)}</td>
          <td class="col-ep"><span class="ep-path" title="${esc(k.endpoint)}">${esc(k.endpoint) || '<span class="muted">—</span>'}</span></td>
          <td class="col-cred">${esc(k.username) || '<span class="muted">—</span>'}</td>
          <td class="col-key">${keyCell(k)}</td>
          <td class="col-expiry">${expiryCell(k)}</td>
          <td class="col-status">${statusCell(k)}</td>
          <td class="col-act">
            <button class="btn-sm" data-test="${i}">Key Gen Test</button>
            <button class="icon-btn" data-edit="${i}" title="Edit">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
            </button>
            <button class="icon-btn danger" data-del="${i}" title="Delete">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
            </button>
          </td>
        </tr>`).join('')
      : `<tr><td colspan="9"><div class="cfg-empty">No API keys yet — click <b>Add API Key</b> to define one.
           ${devices.length ? '' : 'Tip: <a href="settings?tab=devices">add a device first</a>; a key belongs to one.'}</div></td></tr>`;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">API Keys</span>
            <span class="cfg-h-sub">${state.keys.length} key${state.keys.length === 1 ? '' : 's'}
              · one credential per device</span>
          </div>
          <button class="btn-primary btn-sm" id="akAdd" ${devices.length ? '' : 'disabled'}>+ Add API Key</button>
        </div>

        <table class="cfg-table cfg-table-apikey">
          <thead><tr>
            <th class="col-name">Name</th>
            <th class="col-site">Site</th>
            <th class="col-device">Device</th>
            <th class="col-ep">Endpoint</th>
            <th class="col-cred">Credential</th>
            <th class="col-key">Key</th>
            <th class="col-expiry">Expiry</th>
            <th class="col-status">Status</th>
            <th class="col-act"></th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>

        <p class="cfg-foot-note">The issued key is encrypted with the appliance key
          (<code>/etc/pretzel/credentials.key</code>) and stored outside the configuration, so it
          survives a restart and never appears in a config diff or export. The password you type
          is only used to obtain it and is not persisted.</p>
      </div>

      <div class="slideover-overlay" id="akOverlay"></div>
      <aside class="slideover" id="akPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="akTitle">Add API Key</span>
          <button class="slideover-close" id="akClose">&times;</button>
        </div>
        <div class="slideover-body" id="akBody"></div>
        <div class="slideover-foot" id="akFoot"></div>
      </aside>

      <div class="test-result" id="akTestResult"></div>`;

    wire();
  }

  // ── Editor ───────────────────────────────────────────────────────────────────
  function fieldRow(label, key, val, type, ph) {
    return `<div class="field-row"><label>${esc(label)}</label>
        <input type="${type === 'password' ? 'password' : 'text'}"${
          type === 'password' ? ' autocomplete="new-password"' : ''} data-f="${esc(key)}"
          value="${esc(val)}" placeholder="${esc(ph || '')}"/></div>`;
  }

  function editorForm(k) {
    const dev = devices().find(d => d.oid === k.device);
    const spec = credSpec(dev ? dev.device_type : 'ngfw');
    const held = secrets.for(k.oid);

    const siteOpts = ['<option value="">— select a site —</option>'].concat(
      sites().map(s => `<option value="${esc(s.oid)}" ${draftSite === s.oid ? 'selected' : ''}>${esc(s.name)}</option>`)
    ).join('');

    const creds = spec.supported
      ? spec.fields.map(([f, label, type, ph]) =>
          fieldRow(label, f, type === 'password' ? '' : (k[f] || ''), type,
                   type === 'password' ? (held[f] ? '•••••••• entered' : ph) : ph)).join('')
      : `<p class="field-hint">${esc(spec.note || 'Not supported yet.')}</p>`;

    return `
      ${fieldRow('Name', 'name', k.name, 'text', 'e.g. sherpain-fw key')}
      <div class="field-row"><label>Site</label>
        <select data-sitesel>${siteOpts}</select></div>
      <div class="field-row"><label>Device</label>
        <select data-f="device" data-devsel>${deviceOptsForSite(draftSite, k.device)}</select></div>
      ${dev ? `<p class="field-hint">Device type <b>${esc(window.NMS.devices.typeLabel(dev.device_type))}</b>
                 — reached at <code>${esc(dev.target)}</code>.</p>` : ''}

      <div class="editor-sec">KEY GENERATION</div>
      ${fieldRow('Endpoint', 'endpoint', k.endpoint, 'text', spec.keygenHint)}
      <p class="field-hint">Path only — the host comes from the device.</p>

      <div class="editor-sec">CREDENTIAL</div>
      ${creds}`;
  }

  function collect(body) {
    const g = (f) => {
      const el = body.querySelector(`[data-f="${f}"]`);
      return el ? el.value.trim() : '';
    };
    const out = normalize({
      oid: draftOid,
      name: g('name'),
      device: g('device'),
      endpoint: g('endpoint'),
      username: g('username'),
    });

    // Typed passwords go straight to the browser-held store, never into the record.
    const pw = g('password');
    if (pw) secrets.put(draftOid, { password: pw });
    return out;
  }

  function openEditor(idx) {
    editIdx = idx;
    const k = idx == null ? blank() : normalize(JSON.parse(JSON.stringify(state.keys[idx])));
    draftOid = k.oid;
    draftSite = (deviceOf(k) || {}).site || '';   // scope the Device select to the current device's site
    document.getElementById('akTitle').textContent = idx == null ? 'Add API Key' : 'Edit API Key';
    document.getElementById('akBody').innerHTML = editorForm(k);
    document.getElementById('akFoot').innerHTML = `
      ${idx == null ? '' : '<button class="btn-sm btn-danger" id="akDelete">Delete</button>'}
      <span style="flex:1"></span>
      <button class="btn-sm" id="akCancel">Cancel</button>
      <button class="btn-primary btn-sm" id="akSave">Save</button>`;
    wireEditor();
    document.getElementById('akOverlay').classList.add('open');
    document.getElementById('akPanel').classList.add('open');
  }

  const closeEditor = () => {
    document.getElementById('akOverlay').classList.remove('open');
    document.getElementById('akPanel').classList.remove('open');
  };

  function wireEditor() {
    const body = document.getElementById('akBody');

    // Picking a site re-scopes the device list and clears any prior device.
    body.querySelector('[data-sitesel]')?.addEventListener('change', (e) => {
      draftSite = e.target.value;
      const k = collect(body);
      k.device = '';
      body.innerHTML = editorForm(k);
      wireEditor();
    });

    // Device decides the credential shape, so changing it rebuilds the form.
    body.querySelector('[data-devsel]')?.addEventListener('change', () => {
      const k = collect(body);
      const dev = (window.NMS.devices && window.NMS.devices.byOid(k.device)) || null;
      const spec = credSpec(dev ? dev.device_type : 'ngfw');
      if (!k.endpoint && spec.keygenHint) k.endpoint = spec.keygenHint;
      body.innerHTML = editorForm(k);
      wireEditor();
    });

    // Site and Device selects get the themed dropdown (consistent across OSes).
    window.NMS.utils.enhanceSelects(body);

    document.getElementById('akCancel').onclick = closeEditor;
    document.getElementById('akClose').onclick = closeEditor;
    document.getElementById('akOverlay').onclick = closeEditor;

    const del = document.getElementById('akDelete');
    if (del) del.onclick = () => { if (removeKey(editIdx)) { closeEditor(); render(); } };

    document.getElementById('akSave').onclick = () => {
      const k = collect(body);
      if (!k.name) { alert('Name is required.'); return; }
      if (!k.device) { alert('Select the device this key belongs to.'); return; }
      if (!k.endpoint || k.endpoint[0] !== '/') { alert('Endpoint must be a path starting with /'); return; }
      if (editIdx == null) state.keys.push(k); else state.keys[editIdx] = k;
      stage();
      closeEditor(); render();
    };
  }

  function usedByCount(oid) {
    const conns = (window.NMS.apiConnectors && window.NMS.apiConnectors()) || [];
    return conns.filter(c => c.api_key === oid).length;
  }

  function removeKey(idx) {
    const k = state.keys[idx];
    const used = usedByCount(k.oid);
    if (used && !confirm(`${used} connector${used > 1 ? 's' : ''} still reference this key. Delete anyway?`))
      return false;
    secrets.drop(k.oid);
    state.keys.splice(idx, 1);
    stage();
    return true;
  }

  // ── Key generation test ─────────────────────────────────────────────────────
  // Runs from the row: a key is something you keep and re-verify, so it is not buried in an
  // editor. The browser cannot reach a customer's firewall, so mgmtd hands the call to scand and
  // returns a ticket we poll — neither daemon blocks its single loop on a slow device.
  const POLL_MS = 700;
  const POLL_LIMIT = 40;

  async function runDeviceTest(path, payload) {
    const start = await fetch(path, {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    if (start.status === 404) throw new Error('backend test endpoint is not available');
    const started = await start.json().catch(() => null);
    if (!start.ok || !started || !started.ticket)
      throw new Error((started && started.error) || ('HTTP ' + start.status));

    for (let i = 0; i < POLL_LIMIT; i++) {
      await new Promise(r => setTimeout(r, POLL_MS));
      const r = await fetch('/api/connector/test-result?ticket=' + started.ticket,
                            { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      const d = await r.json().catch(() => null);
      if (d && d.status === 'done') return d;
    }
    throw new Error('timed out waiting for the device');
  }

  function stepRow(label, st) {
    const mark = !st ? '<span class="ts-idle">·</span>'
               : st.ok ? '<span class="ts-ok">✓</span>'
               : '<span class="ts-fail">✗</span>';
    return `<div class="ts-row"><span class="ts-mark">${mark}</span>
              <span class="ts-label">${label}</span>
              <span class="ts-detail">${esc((st && st.detail) || '')}</span></div>`;
  }

  async function runKeygenTest(idx) {
    const k = state.keys[idx];
    const dev = deviceOf(k);
    if (!dev) { alert('This key references a device that no longer exists.'); return; }

    const held = secrets.for(k.oid);
    if (!held.password) { alert('Enter the password first — edit the key and save it.'); return; }

    const modal = window.NMS.modal;
    const body = (inner) => `<div class="test-panel-wrap">${inner}</div>`;
    modal.open('API Key Generation Test',
      body(`<div class="test-panel running">${stepRow('TLS connection', null)}${stepRow('API key generation', null)}</div>`));

    let res;
    try {
      res = await runDeviceTest('/api/connector/keygen-test', {
        oid: k.oid,
        target: dev.target,
        fingerprint: dev.fingerprint,
        endpoint: k.endpoint,
        secrets: { username: k.username, password: held.password },
      });
    } catch (e) {
      secrets.put(k.oid, { last_test: { at: Date.now(), ok: false, detail: e.message } });
      render();
      modal.open('API Key Generation Test', body(`<div class="test-panel err"><div class="ts-note">${esc(e.message)}</div></div>`));
      return;
    }

    const steps = res.steps || {};

    // A first contact stops at the certificate — HttpClient will not transmit credentials to an
    // unpinned peer. The pin belongs to the device, so confirming it updates the device.
    const trust = (res.fingerprint && !res.fingerprint_trusted)
      ? `<div class="fp-prompt">
           <div class="fp-warn">Certificate is not trusted yet (self-signed)</div>
           <code class="fp-val">${esc(res.fingerprint)}</code>
           <div class="fp-sub">${esc(res.cert_subject || '')}</div>
           <button class="btn-sm btn-primary" id="akTrustFp">Trust this certificate</button>
         </div>`
      : '';

    // Recording an outcome must not dirty the configuration — it is state, not a declaration.
    // `stored` reflects what engined actually kept; a missing credentials.key means the test
    // succeeded but nothing was persisted, and the row should say so rather than imply otherwise.
    secrets.put(k.oid, {
      last_test: { at: Date.now(), ok: !!res.ok, detail: res.message || '' },
      ...(res.ok ? { stored: !!res.stored, issued_at: Date.now(), expires_at: res.expires_at || null } : {}),
    });

    modal.open('API Key Generation Test', body(`
      <div class="test-panel ${res.ok ? 'ok' : 'err'}">
        ${stepRow('TLS connection', steps.tls)}
        ${stepRow('API key generation', steps.auth)}
        ${trust}
      </div>`));

    document.getElementById('akTrustFp')?.addEventListener('click', (e) => {
      window.NMS.devices.pinFingerprint(dev.oid, res.fingerprint);
      e.currentTarget.outerHTML = '<span class="fp-trusted">Pinned to the device — run the test again.</span>';
    });

    render();
  }

  function wire() {
    const el = document.getElementById('contentBody');
    document.getElementById('akAdd')?.addEventListener('click', () => openEditor(null));
    el.querySelectorAll('[data-edit]').forEach(b =>
      b.addEventListener('click', () => openEditor(+b.dataset.edit)));
    el.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', () => {
      if (removeKey(+b.dataset.del)) render();
    }));
    el.querySelectorAll('[data-test]').forEach(b =>
      b.addEventListener('click', () => runKeygenTest(+b.dataset.test)));
  }

  // ── Init ─────────────────────────────────────────────────────────────────────
  const keysRefresh = async () => { await load(); render(); };

  function activate() {
    render();
    window.NMS.onRefresh(keysRefresh);
  }

  document.addEventListener('DOMContentLoaded', async () => {
    await load();
    if (activeTab() === 'api-key') activate();
    document.dispatchEvent(new Event('nms:api-keys-ready'));
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === 'api-key') activate();
  });

  // Device names and the connector usage count load independently.
  document.addEventListener('nms:devices-ready', () => { if (activeTab() === 'api-key') render(); });
  document.addEventListener('nms:sites-ready', () => { if (activeTab() === 'api-key') render(); });
  document.addEventListener('nms:connectors-ready', () => { if (activeTab() === 'api-key') render(); });
})();
