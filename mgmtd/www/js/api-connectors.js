/* api-connectors.js — Configuration ▸ API Connector.
 *
 * The last step of the configuration flow (Site → Inventory → Authentication → API Connector):
 * a Connector is what decides that a given inventory object is reached over its API — it binds
 * the object to the auth profile used against it, plus collection settings (poll interval,
 * enabled). Inventory itself holds no credentials; an object without a connector is monitored
 * by ICMP only.
 *
 * The connection test lives here for the same reason: validating a credential needs both a host
 * (from the object) and a profile, and only a connector has both.
 *
 * Committed under scand.service.api.connectors (same domain as auth_profiles). Loaded on every
 * Configuration tab; rendered only on its own tab.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // Read live (not once): settings tabs switch client-side without a page load (see main.js).
  const activeTab = () => new URLSearchParams(location.search).get('tab') || 'sites';
  const DRAFT_KEY = 'api_connectors';
  const DEFAULT_INTERVAL_SEC = 60;
  const MIN_INTERVAL_SEC = 5;

  const { esc, newUuid } = window.NMS.utils;

  const state = { connectors: [] };
  let deployed = [];
  let editIdx = null;
  let draftOid = null;

  // PAN-OS serves two APIs behind one credential. The operator states which one and the exact
  // path, because customer devices run versions we do not control — the REST path carries the
  // version (/restapi/v10.2/…), the XML path does not (/api/?type=…).
  // [key, label, path placeholder, parameters PAN-OS requires for a typical call]
  const API_TYPES = [
    ['rest', 'REST API', '/restapi/v10.2/Objects/Addresses',
      [{ name: 'location', value: 'vsys' }, { name: 'vsys', value: 'vsys1' }]],
    ['xml', 'XML API', '/api/',
      [{ name: 'type', value: 'op' }, { name: 'cmd', value: '<show><system><info></info></system></show>' }]],
  ];
  const apiTypeRow = (t) => API_TYPES.find(([k]) => k === t) || API_TYPES[0];
  const apiTypeLabel = (t) => apiTypeRow(t)[1];
  const apiTypeHint = (t) => apiTypeRow(t)[2];
  const apiTypeParams = (t) => apiTypeRow(t)[3];

  // The URL the connector will call, for display. Encoding matches what mgmtd does server-side.
  function effectivePath(c) {
    let path = c.endpoint || '';
    (c.params || []).forEach(p => {
      if (!p.name) return;
      path += (path.indexOf('?') === -1) ? '?' : '&';
      path += encodeURIComponent(p.name) + '=' + encodeURIComponent(p.value);
    });
    return path;
  }

  function normalize(c) {
    const interval = parseInt(c.poll_interval_sec, 10);
    return {
      // `uuid` = legacy key; a numeric oid predates the merge and is replaced.
      oid: (typeof c.oid === 'string' && c.oid) ? c.oid : (c.uuid || newUuid()),
      name: c.name || '',
      description: c.description || '',
      object: c.object || '',              // inventory object oid
      auth_profile: c.auth_profile || '',  // Authentication profile oid
      api_type: c.api_type === 'xml' ? 'xml' : 'rest',
      endpoint: c.endpoint || '',          // operator-entered path, without a query string
      // Query parameters as name/value pairs — stored unencoded, encoded when the URL is built,
      // so the operator can type raw values (PAN-OS cmd= XML included).
      params: (Array.isArray(c.params) ? c.params : [])
        .map(p => ({ name: String((p && p.name) || ''), value: String((p && p.value) || '') }))
        .filter(p => p.name),
      poll_interval_sec: (Number.isInteger(interval) && interval >= MIN_INTERVAL_SEC) ? interval : DEFAULT_INTERVAL_SEC,
      enabled: c.enabled !== false,
      // Set when the endpoint test last passed; a connector is only saved after it does.
      last_test: c.last_test || null,
    };
  }

  const blank = () => normalize({});

  // ── Staging ──────────────────────────────────────────────────────────────────
  const stage = () => { window.NMS.draft.set(DRAFT_KEY, state.connectors); refreshPending(); };
  const refreshPending = () => window.NMS.staging.refresh();

  // ── Data load ────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const r = await fetch('/api/settings', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();
      window.NMS.draft.checkBase(d.version);
      const api = ((d.daemons || {}).scand || {}).api || {};
      deployed = (Array.isArray(api.connectors) ? api.connectors : []).map(normalize);
    } catch (_) { deployed = []; }
    const staged = window.NMS.draft.get(DRAFT_KEY, null);
    state.connectors = Array.isArray(staged) ? staged.map(normalize) : JSON.parse(JSON.stringify(deployed));
    refreshPending();
  }

  const commitPayload = () => [{ daemon: 'scand', domain: 'api', values: { connectors: state.connectors } }];

  // ── Staging provider ─────────────────────────────────────────────────────────
  window.NMS.staging.register({
    key: DRAFT_KEY,
    dirty: () => JSON.stringify(state.connectors) !== JSON.stringify(deployed),
    payload: commitPayload,
    before: () => ({ connectors: deployed }),
    after: () => ({ connectors: state.connectors }),
    onPublished() {
      deployed = JSON.parse(JSON.stringify(state.connectors));
      window.NMS.draft.clear(DRAFT_KEY);
    },
  });

  // ── Reference resolution ─────────────────────────────────────────────────────
  // Any inventory object can be bound; the connector supplies the credential, so the picker
  // offers the whole inventory rather than a pre-filtered subset.
  const objects = () => (window.NMS.devices && window.NMS.devices.list()) || [];
  const profiles = () => (window.NMS.apiKeys && window.NMS.apiKeys.list()) || [];
  const objectByOid = (oid) => objects().find(o => o.oid === oid) || null;

  // Bindable = not already taken by another connector (one collector per object).
  const boundElsewhere = (oid, selfOid) =>
    state.connectors.some(c => c.object === oid && c.oid !== selfOid);

  function objectCell(c) {
    const o = objectByOid(c.object);
    if (!o) return `<span class="authp-missing" title="Object ${esc(c.object)} no longer exists">missing</span>`;
    return `<div class="cell-name">${esc(o.name) || esc(o.target)}</div>
            <div class="cell-sub">${esc(o.target)}</div>`;
  }

  function profileCell(c) {
    if (!c.auth_profile) return `<span class="authp-missing">none</span>`;
    const name = window.NMS.apiKeys && window.NMS.apiKeys.label(c.auth_profile);
    return name
      ? `<span class="authp-chip">${esc(name)}</span>`
      : `<span class="authp-missing" title="Profile ${esc(c.auth_profile)} no longer exists">missing</span>`;
  }

  // ── Render ───────────────────────────────────────────────────────────────────
  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'api-connector') return;

    const ready = objects().length && profiles().length;
    const emptyHint = ready
      ? 'click <b>Add Connector</b> to bind an object to a credential.'
      : `define an <a href="settings?tab=devices">device</a> and an
         <a href="settings?tab=api-key">API key</a> first, then bind them here.`;

    const rows = state.connectors.length
      ? state.connectors.map((c, i) => `
        <tr class="${c.enabled ? '' : 'row-off'}">
          <td class="col-name">
            <div class="cell-name">${esc(c.name) || '<span class="muted">unnamed</span>'}</div>
            ${c.description ? `<div class="cell-sub">${esc(c.description)}</div>` : ''}
          </td>
          <td class="col-obj">${objectCell(c)}</td>
          <td class="col-authp">${profileCell(c)}</td>
          <td class="col-api"><span class="api-badge ${esc(c.api_type)}">${esc(apiTypeLabel(c.api_type))}</span></td>
          <td class="col-ep"><span class="ep-path" title="${esc(effectivePath(c))}">${esc(effectivePath(c))}</span></td>
          <td class="col-int">${c.poll_interval_sec}s</td>
          <td class="col-en">
            <label class="tgl" title="${c.enabled ? 'Enabled' : 'Disabled'}">
              <input type="checkbox" data-toggle="${i}" ${c.enabled ? 'checked' : ''}/>
              <span class="tgl-track"></span>
            </label>
          </td>
          <td class="col-act">
            <button class="icon-btn" data-edit="${i}" title="Edit">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
            </button>
            <button class="icon-btn danger" data-del="${i}" title="Delete">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
            </button>
          </td>
        </tr>`).join('')
      : `<tr><td colspan="7"><div class="cfg-empty">No API connectors yet — ${emptyHint}</div></td></tr>`;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">API Connectors</span>
            <span class="cfg-h-sub">${state.connectors.length} connector${state.connectors.length === 1 ? '' : 's'}
              · ${objects().length} inventory object${objects().length === 1 ? '' : 's'} available</span>
          </div>
          <button class="btn-primary btn-sm" id="acAdd" ${ready ? '' : 'disabled'}>+ Add Connector</button>
        </div>

        <table class="cfg-table cfg-table-conn">
          <thead><tr>
            <th class="col-name">Name</th>
            <th class="col-obj">Object</th>
            <th class="col-authp">Auth Profile</th>
            <th class="col-api">API</th>
            <th class="col-ep">Endpoint</th>
            <th class="col-int">Interval</th>
            <th class="col-en">Enable</th>
            <th class="col-act"></th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>

      <div class="slideover-overlay" id="acOverlay"></div>
      <aside class="slideover" id="acPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="acTitle">Add Connector</span>
          <button class="slideover-close" id="acClose">&times;</button>
        </div>
        <div class="slideover-body" id="acBody"></div>
        <div class="slideover-foot" id="acFoot"></div>
      </aside>`;

    wire();
  }

  // ── Editor ───────────────────────────────────────────────────────────────────
  // Creating a connector is a gated sequence, because a credential that works and an endpoint
  // that works are independent things and the operator needs to know which one failed:
  //
  //   API Key Generation Test  ──ok──▶  Endpoint Test  ──ok──▶  Save
  //
  // Each gate unlocks the next; editing any field that feeds a passed gate invalidates it, so
  // a connector can never be saved against a path or credential that was never proven. On a
  // first contact the keygen test stops at the certificate — HttpClient will not transmit
  // credentials to an unpinned peer — and the operator confirms the fingerprint first.
  const GATE = { idle: 'idle', running: 'running', ok: 'ok', fail: 'fail' };
  let gate = { keygen: GATE.idle, endpoint: GATE.idle };

  function resetGates(from) {
    if (from === 'keygen') gate = { keygen: GATE.idle, endpoint: GATE.idle };
    else gate.endpoint = GATE.idle;
    paintGates();
  }

  function paintGates() {
    const keygenBtn = document.getElementById('acKeygenBtn');
    const epBtn = document.getElementById('acEndpointBtn');
    const saveBtn = document.getElementById('acSave');
    if (!keygenBtn) return;

    const c = collect(document.getElementById('acBody'));
    const haveCred = !!(c.object && c.auth_profile);

    keygenBtn.disabled = !haveCred || gate.keygen === GATE.running;
    epBtn.disabled = gate.keygen !== GATE.ok || !c.endpoint || gate.endpoint === GATE.running;
    epBtn.title = gate.keygen !== GATE.ok ? 'Run the API key generation test first' : '';
    if (saveBtn) saveBtn.disabled = gate.endpoint !== GATE.ok;

    document.getElementById('acKeygenState').className = 'gate-state ' + gate.keygen;
    document.getElementById('acEndpointState').className = 'gate-state ' + gate.endpoint;
  }

  function paramRow(p) {
    return `<div class="param-row">
        <input data-p="name" data-gate="endpoint" value="${esc(p.name)}" placeholder="name"/>
        <input data-p="value" data-gate="endpoint" value="${esc(p.value)}" placeholder="value"/>
        <button class="icon-btn danger" data-prm-del type="button" title="Remove">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        </button>
      </div>`;
  }

  function editorForm(c) {
    const objOpts = ['<option value="">— select an object —</option>'].concat(
      objects().map(o => {
        const taken = boundElsewhere(o.oid, c.oid);
        const label = `${o.name || o.target} (${o.target})${taken ? ' — already bound' : ''}`;
        return `<option value="${esc(o.oid)}" ${c.object === o.oid ? 'selected' : ''} ${taken ? 'disabled' : ''}>${esc(label)}</option>`;
      })
    ).join('');

    const profOpts = ['<option value="">— select a profile —</option>'].concat(
      profiles().map(p =>
        `<option value="${esc(p.oid)}" ${c.auth_profile === p.oid ? 'selected' : ''}>${esc(p.name)}</option>`)
    ).join('');

    const apiOpts = API_TYPES.map(([k, label]) =>
      `<option value="${esc(k)}" ${c.api_type === k ? 'selected' : ''}>${esc(label)}</option>`).join('');

    return `
      <div class="field-row"><label>Name</label>
        <input data-f="name" value="${esc(c.name)}" placeholder="e.g. core-fw-01 address objects"/></div>
      <div class="field-row"><label>Description</label>
        <input data-f="description" value="${esc(c.description)}" placeholder="optional"/></div>
      <label class="field-check"><input type="checkbox" data-f="enabled" ${c.enabled ? 'checked' : ''}/><span>Enabled</span></label>

      <div class="editor-sec">1 · TARGET &amp; CREDENTIAL</div>
      <div class="field-row"><label>Object</label>
        <select data-f="object" data-gate="keygen">${objOpts}</select></div>
      <div class="field-row"><label>Auth Profile</label>
        <select data-f="auth_profile" data-gate="keygen">${profOpts}</select></div>
      <div class="gate-row">
        <button class="btn-sm" id="acKeygenBtn">API Key Generation Test</button>
        <span class="gate-state idle" id="acKeygenState"></span>
      </div>
      <div class="test-result" id="acKeygenResult"></div>

      <div class="editor-sec">2 · ENDPOINT</div>
      <div class="field-row"><label>API</label>
        <select data-f="api_type" data-gate="endpoint" data-apisel>${apiOpts}</select></div>
      <div class="field-row"><label>Endpoint path</label>
        <input data-f="endpoint" data-gate="endpoint" value="${esc(c.endpoint)}"
               placeholder="${esc(apiTypeHint(c.api_type))}"/></div>

      <div class="param-head">
        <label>Query parameters</label>
        <button class="btn-sm" id="acParamAdd" type="button">+ Param</button>
      </div>
      <div class="param-list" id="acParamList">${(c.params.length ? c.params : [{ name: '', value: '' }])
        .map(paramRow).join('')}</div>
      <p class="field-hint" id="acEpHint">Values are percent-encoded for you — type them raw.
        The API key is added automatically (${c.api_type === 'xml' ? '<code>key=</code> parameter'
                                                                  : '<code>X-PAN-KEY</code> header'}).</p>
      <div class="ep-preview"><span class="ep-preview-h">Will call</span>
        <code id="acEpPreview">${esc(effectivePath(c)) || '—'}</code></div>

      <div class="gate-row">
        <button class="btn-sm" id="acEndpointBtn">Endpoint Test</button>
        <span class="gate-state idle" id="acEndpointState"></span>
      </div>
      <div class="test-result" id="acEndpointResult"></div>

      <div class="editor-sec">3 · COLLECTION</div>
      <div class="field-row"><label>Poll interval (sec)</label>
        <input data-f="poll_interval_sec" value="${c.poll_interval_sec}" placeholder="${DEFAULT_INTERVAL_SEC}"/></div>`;
  }

  function collect(body) {
    const g = (k) => {
      const e = body.querySelector(`[data-f="${k}"]`);
      return e ? (e.type === 'checkbox' ? e.checked : e.value.trim()) : undefined;
    };
    const prev = editIdx == null ? null : state.connectors[editIdx];
    const params = Array.from(body.querySelectorAll('.param-row')).map(row => ({
      name: (row.querySelector('[data-p="name"]').value || '').trim(),
      value: (row.querySelector('[data-p="value"]').value || '').trim(),
    }));

    return normalize({
      oid: draftOid,
      name: g('name'),
      description: g('description'),
      object: g('object'),
      auth_profile: g('auth_profile'),
      api_type: g('api_type'),
      endpoint: g('endpoint'),
      params,
      poll_interval_sec: g('poll_interval_sec'),
      enabled: g('enabled') !== false,
      last_test: prev ? prev.last_test : null,
    });
  }

  function openEditor(idx) {
    editIdx = idx;
    const c = idx == null ? blank() : normalize(JSON.parse(JSON.stringify(state.connectors[idx])));
    draftOid = c.oid;

    // An existing connector was already proven when it was created; re-testing is only forced
    // once the operator edits something the proof depended on.
    gate = (idx != null && c.last_test && c.last_test.ok)
      ? { keygen: GATE.ok, endpoint: GATE.ok }
      : { keygen: GATE.idle, endpoint: GATE.idle };

    document.getElementById('acTitle').textContent = idx == null ? 'Add Connector' : 'Edit Connector';
    document.getElementById('acBody').innerHTML = editorForm(c);
    document.getElementById('acFoot').innerHTML = `
      ${idx == null ? '' : '<button class="btn-sm btn-danger" id="acDelete">Delete</button>'}
      <span style="flex:1"></span>
      <button class="btn-sm" id="acCancel">Cancel</button>
      <button class="btn-primary btn-sm" id="acSave">Save</button>`;
    wireEditor();
    document.getElementById('acOverlay').classList.add('open');
    document.getElementById('acPanel').classList.add('open');
  }

  const closeEditor = () => {
    document.getElementById('acOverlay').classList.remove('open');
    document.getElementById('acPanel').classList.remove('open');
  };

  // Bind one field so editing it invalidates the gate it fed (and everything downstream), and
  // keeps the "Will call" preview honest.
  function bindGateField(el) {
    const evt = el.tagName === 'SELECT' ? 'change' : 'input';
    el.addEventListener(evt, () => {
      resetGates(el.dataset.gate);
      document.getElementById('acKeygenResult').innerHTML = '';
      document.getElementById('acEndpointResult').innerHTML = '';
      refreshPreview();
    });
  }

  function refreshPreview() {
    const el = document.getElementById('acEpPreview');
    if (el) el.textContent = effectivePath(collect(document.getElementById('acBody'))) || '—';
  }

  function wireParamRow(row) {
    row.querySelectorAll('[data-gate]').forEach(bindGateField);
    row.querySelector('[data-prm-del]').addEventListener('click', () => {
      row.remove();
      resetGates('endpoint');
      refreshPreview();
      paintGates();
    });
  }

  function wireEditor() {
    const body = document.getElementById('acBody');

    // Any field a gate depended on invalidates that gate (and everything downstream of it).
    body.querySelectorAll('[data-gate]').forEach(bindGateField);
    body.querySelectorAll('.param-row').forEach(row => {
      row.querySelector('[data-prm-del]').addEventListener('click', () => {
        row.remove();
        resetGates('endpoint');
        refreshPreview();
        paintGates();
      });
    });

    document.getElementById('acParamAdd').onclick = () => {
      const list = document.getElementById('acParamList');
      list.insertAdjacentHTML('beforeend', paramRow({ name: '', value: '' }));
      wireParamRow(list.lastElementChild);
      list.lastElementChild.querySelector('[data-p="name"]').focus();
    };

    // Switching API swaps the placeholder, the key-delivery note, and — when the operator has
    // not typed anything yet — seeds the parameters that call normally needs.
    body.querySelector('[data-apisel]')?.addEventListener('change', (e) => {
      const type = e.target.value;
      const input = body.querySelector('[data-f="endpoint"]');
      const untouched = input && (!input.value.trim() || API_TYPES.some(([, , hint]) => hint === input.value.trim()));
      if (input) {
        input.placeholder = apiTypeHint(type);
        if (untouched) input.value = apiTypeHint(type);
      }

      const list = document.getElementById('acParamList');
      const current = collect(body).params.filter(p => p.name);
      const seedable = untouched || current.length === 0 ||
        API_TYPES.some(([, , , seed]) => JSON.stringify(seed) === JSON.stringify(current));
      if (list && seedable) {
        list.innerHTML = apiTypeParams(type).map(paramRow).join('');
        list.querySelectorAll('.param-row').forEach(wireParamRow);
      }

      const hint = document.getElementById('acEpHint');
      if (hint) hint.innerHTML = `Values are percent-encoded for you — type them raw.
        The API key is added automatically (${type === 'xml' ? '<code>key=</code> parameter'
                                                            : '<code>X-PAN-KEY</code> header'}).`;
      refreshPreview();
    });

    document.getElementById('acKeygenBtn').onclick = () => runKeygenTest(body);
    document.getElementById('acEndpointBtn').onclick = () => runEndpointTest(body);

    document.getElementById('acCancel').onclick = closeEditor;
    document.getElementById('acClose').onclick = closeEditor;
    document.getElementById('acOverlay').onclick = closeEditor;

    const del = document.getElementById('acDelete');
    if (del) del.onclick = () => {
      state.connectors.splice(editIdx, 1);
      closeEditor(); stage(); render();
    };

    document.getElementById('acSave').onclick = () => {
      const c = collect(body);
      if (gate.endpoint !== GATE.ok) { alert('Run the endpoint test before saving.'); return; }
      c.last_test = { at: Date.now(), ok: true };
      if (editIdx == null) state.connectors.push(c); else state.connectors[editIdx] = c;
      stage();
      closeEditor(); render();
    };

    paintGates();
  }

  // ── Device tests ────────────────────────────────────────────────────────────
  // mgmtd performs the call — the browser cannot reach a customer's firewall, and the daemon
  // must not block its single loop on a slow device, so the request returns a ticket and we
  // poll for the outcome.
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
    if (!start.ok || !started || !started.ticket) {
      throw new Error((started && started.error) || ('HTTP ' + start.status));
    }

    for (let i = 0; i < POLL_LIMIT; i++) {
      await new Promise(r => setTimeout(r, POLL_MS));
      const r = await fetch('/api/connector/test-result?ticket=' + started.ticket,
                            { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      const d = await r.json().catch(() => null);
      if (d && d.status === 'done') return d;
    }
    throw new Error('timed out waiting for the device');
  }

  // Both tests need the same device coordinates: the host from the object, the credential and
  // TLS policy from the profile, and the secret from the browser-held store.
  function testPayload(c) {
    const o = objectByOid(c.object);
    const p = window.NMS.apiKeys ? window.NMS.apiKeys.byOid(c.auth_profile) : null;
    return {
      target: o ? o.target : '',
      fingerprint: o ? o.fingerprint : '',
      username: p ? p.username : '',
      secrets: window.NMS.apiKeySecrets ? window.NMS.apiKeySecrets.for(c.auth_profile) : {},
    };
  }

  function stepRow(label, st) {
    const mark = !st ? '<span class="ts-idle">·</span>'
               : st.ok ? '<span class="ts-ok">✓</span>'
               : '<span class="ts-fail">✗</span>';
    return `<div class="ts-row"><span class="ts-mark">${mark}</span>
              <span class="ts-label">${label}</span>
              <span class="ts-detail">${esc((st && st.detail) || '')}</span></div>`;
  }

  // A device we have never pinned stops the test at the certificate. The operator confirms the
  // fingerprint, it is stored on the profile, and the test is run again — only then does the
  // credential leave this machine.
  function trustPrompt(res, c) {
    if (!res.fingerprint || res.fingerprint_trusted) return '';
    return `<div class="fp-prompt">
        <div class="fp-warn">Certificate is not trusted yet (self-signed)</div>
        <code class="fp-val">${esc(res.fingerprint)}</code>
        <div class="fp-sub">${esc(res.cert_subject || '')}</div>
        <button class="btn-sm btn-primary" id="acTrustFp" data-device="${esc(c.object)}"
                data-fp="${esc(res.fingerprint)}">Trust this certificate</button>
      </div>`;
  }

  function wireTrust(containerId) {
    document.getElementById('acTrustFp')?.addEventListener('click', (e) => {
      const btn = e.currentTarget;
      window.NMS.devices.pinFingerprint(btn.dataset.device, btn.dataset.fp);
      document.getElementById(containerId).innerHTML +=
        `<div class="ts-note">Fingerprint pinned to the profile — run the test again.</div>`;
      btn.outerHTML = '<span class="fp-trusted">Pinned</span>';
    });
  }

  async function runKeygenTest(body) {
    const c = collect(body);
    const out = document.getElementById('acKeygenResult');
    gate.keygen = GATE.running; gate.endpoint = GATE.idle; paintGates();
    out.innerHTML = `<div class="test-panel running">${stepRow('TLS connection', null)}${stepRow('API key generation', null)}</div>`;

    let res;
    try {
      res = await runDeviceTest('/api/connector/keygen-test', testPayload(c));
    } catch (e) {
      gate.keygen = GATE.fail; paintGates();
      out.innerHTML = `<div class="test-panel err"><div class="ts-note">${esc(e.message)}</div></div>`;
      return;
    }

    const steps = res.steps || {};
    gate.keygen = res.ok ? GATE.ok : GATE.fail;
    out.innerHTML = `<div class="test-panel ${res.ok ? 'ok' : 'err'}">
        ${stepRow('TLS connection', steps.tls)}
        ${stepRow('API key generation', steps.auth)}
        ${trustPrompt(res, c)}
      </div>`;
    wireTrust('acKeygenResult');
    paintGates();

  }

  // Pretty-print when the body is JSON; XML and error text are shown as returned.
  function formatBody(raw) {
    const text = String(raw == null ? '' : raw).trim();
    if (!text || (text[0] !== '{' && text[0] !== '[')) return text;
    try { return JSON.stringify(JSON.parse(text), null, 2); } catch (_) { return text; }
  }

  // The endpoint the operator typed is the thing being debugged, so the outcome gets its own
  // window rather than a cramped strip inside the editor: the exact request line on top, the
  // full response below. Kept in `lastEndpointResult` so it can be reopened without re-running
  // the test against the customer's device.
  let lastEndpointResult = null;

  function showEndpointDetail() {
    const res = lastEndpointResult;
    if (!res || !window.NMS.modal) return;

    const req = res.request || {};
    const rsp = res.response || {};
    const statusClass = res.ok ? 'ok' : 'err';

    window.NMS.modal.open('Endpoint test', `
      <div class="ep-detail">
        <div class="ep-block">
          <div class="ep-block-h">Request</div>
          <pre class="ep-req">${esc(req.method || 'GET')} ${esc(req.url || '')}</pre>
          <div class="ep-note">API key sent via ${esc(req.key_delivery || '')} — redacted above.</div>
        </div>

        <div class="ep-block">
          <div class="ep-block-h">Response
            <span class="ep-status ${statusClass}">HTTP ${esc(rsp.status != null ? rsp.status : '—')}</span>
            ${rsp.bytes != null ? `<span class="ep-bytes">${esc(rsp.bytes)} bytes${rsp.truncated ? ', truncated' : ''}</span>` : ''}
          </div>
          <pre class="ep-rsp">${esc(formatBody(rsp.body))}</pre>
        </div>
      </div>`);
  }

  async function runEndpointTest(body) {
    const c = collect(body);
    const out = document.getElementById('acEndpointResult');
    gate.endpoint = GATE.running; paintGates();
    out.innerHTML = `<div class="test-panel running">${stepRow('Endpoint response', null)}</div>`;

    let res;
    try {
      const payload = testPayload(c);
      payload.api_type = c.api_type;
      payload.endpoint = c.endpoint;
      payload.params = c.params;
      res = await runDeviceTest('/api/connector/endpoint-test', payload);
    } catch (e) {
      gate.endpoint = GATE.fail; paintGates();
      lastEndpointResult = null;
      out.innerHTML = `<div class="test-panel err"><div class="ts-note">${esc(e.message)}</div></div>`;
      return;
    }

    const steps = res.steps || {};
    gate.endpoint = res.ok ? GATE.ok : GATE.fail;
    lastEndpointResult = res;

    // The editor keeps only the verdict; the detail lives in the window.
    out.innerHTML = `<div class="test-panel ${res.ok ? 'ok' : 'err'}">
        ${stepRow('TLS connection', steps.tls)}
        ${stepRow('API key generation', steps.auth)}
        ${stepRow('Endpoint response', steps.endpoint)}
        ${trustPrompt(res, c)}
        ${res.request ? `<div class="ts-more"><button class="btn-sm" id="acEpDetail">View request / response</button></div>` : ''}
      </div>`;
    wireTrust('acEndpointResult');
    document.getElementById('acEpDetail')?.addEventListener('click', showEndpointDetail);
    paintGates();

    // Raise the window straight away — the operator ran the test to see this.
    if (res.request) showEndpointDetail();
  }

  // ── Wiring (table) ──────────────────────────────────────────────────────────
  // render() replaces #contentBody wholesale, so every row control is re-bound here on each
  // paint; render() calls this as its last step.
  function wire() {
    const el = document.getElementById('contentBody');
    document.getElementById('acAdd')?.addEventListener('click', () => openEditor(null));
    el.querySelectorAll('[data-edit]').forEach(b =>
      b.addEventListener('click', () => openEditor(+b.dataset.edit)));
    el.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', () => {
      state.connectors.splice(+b.dataset.del, 1); stage(); render();
    }));
    el.querySelectorAll('[data-toggle]').forEach(cb => cb.addEventListener('change', () => {
      state.connectors[+cb.dataset.toggle].enabled = cb.checked; stage(); render();
    }));
  }

  // ── Init ─────────────────────────────────────────────────────────────────────
  const connectorRefresh = async () => { await load(); render(); };

  function activate() {
    render();
    window.NMS.onRefresh(connectorRefresh);
  }

  document.addEventListener('DOMContentLoaded', async () => {
    await load();
    if (activeTab() === 'api-connector') activate();
    document.dispatchEvent(new Event('nms:connectors-ready'));
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === 'api-connector') activate();
  });

  // Object/profile names come from the other tabs' data, which loads independently.
  document.addEventListener('nms:devices-ready', () => { if (activeTab() === 'api-connector') render(); });
  document.addEventListener('nms:api-keys-ready', () => { if (activeTab() === 'api-connector') render(); });
})();
