/* api-connectors.js — Configuration ▸ API Connector.
 *
 * The last step of the configuration flow (Site → Inventory → Authentication → API Endpoint →
 * API Connector). A connector is the collection POLICY for one inventory object: which
 * credential to use against it, and which endpoints to poll how often. One connector per
 * object, so a device's whole schedule is in one place; an object without a connector is
 * monitored by ICMP only.
 *
 * It holds no paths and no query parameters. An endpoint defined on the API Endpoint page is a
 * complete request, so two firewalls needing different arguments are two endpoints — which is
 * what makes a PAN-OS upgrade a re-point here rather than an edit everywhere.
 *
 * Committed under scand.service.api.connectors.
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

  // ── Model ────────────────────────────────────────────────────────────────────
  function normalizeItem(i) {
    const interval = parseInt(i && i.poll_interval_sec, 10);
    return {
      endpoint: (i && typeof i.endpoint === 'string') ? i.endpoint : '',
      poll_interval_sec: (Number.isInteger(interval) && interval >= MIN_INTERVAL_SEC)
        ? interval : DEFAULT_INTERVAL_SEC,
      enabled: !i || i.enabled !== false,
    };
  }

  function normalize(c) {
    return {
      // `uuid` = legacy key; a numeric oid predates the merge and is replaced.
      oid: (typeof c.oid === 'string' && c.oid) ? c.oid : (c.uuid || newUuid()),
      name: c.name || '',
      description: c.description || '',
      object: c.object || '',              // inventory object oid
      auth_profile: c.auth_profile || '',  // API Key profile oid
      // What this device is collected for, each entry on its own schedule.
      items: (Array.isArray(c.items) ? c.items : []).map(normalizeItem).filter(i => i.endpoint),
    };
  }

  const blank = () => normalize({});

  // ── Staging ──────────────────────────────────────────────────────────────────
  const stage = () => { window.NMS.draft.set(DRAFT_KEY, state.connectors); refreshPending(); };
  const refreshPending = () => window.NMS.staging.refresh();

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
  const objects = () => (window.NMS.devices && window.NMS.devices.list()) || [];
  const profiles = () => (window.NMS.apiKeys && window.NMS.apiKeys.list()) || [];
  const objectByOid = (oid) => objects().find(o => o.oid === oid) || null;

  const endpointList = () => (window.NMS.apiEndpoints && window.NMS.apiEndpoints.list()) || [];
  const endpointByOid = (oid) => (window.NMS.apiEndpoints && window.NMS.apiEndpoints.byOid(oid)) || null;
  const endpointName = (oid) => { const e = endpointByOid(oid); return e ? e.name : ''; };

  // The path with the endpoint's own parameters applied — what the device will actually be
  // asked for, and what the test sends.
  function endpointPath(oid) {
    const e = endpointByOid(oid);
    if (!e) return '';
    let path = e.path || '';
    (e.params || []).forEach(p => {
      if (!p || !p.name) return;
      path += (path.indexOf('?') === -1) ? '?' : '&';
      path += encodeURIComponent(p.name) + '=' + encodeURIComponent(p.value || '');
    });
    return path;
  }

  const endpointApiType = (oid) => {
    const e = endpointByOid(oid);
    return (e && String(e.path || '').indexOf('/restapi/') === 0) ? 'rest' : 'xml';
  };

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

  const anyEnabled = (c) => (c.items || []).some(i => i.enabled);

  function scheduleCell(c) {
    const items = c.items || [];
    if (!items.length) return '<span class="authp-missing">collects nothing</span>';

    return items.map(i => {
      const name = endpointName(i.endpoint);
      const label = name
        ? `<span class="ep-name">${esc(name)}</span>`
        : `<span class="authp-missing" title="Endpoint ${esc(i.endpoint)} no longer exists">missing</span>`;
      return `<div class="sched-row${i.enabled ? '' : ' off'}" title="${esc(endpointPath(i.endpoint))}">
          ${label}<span class="sched-int">${esc(i.poll_interval_sec)}s</span>
        </div>`;
    }).join('');
  }

  // ── Render ───────────────────────────────────────────────────────────────────
  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'api-connector') return;

    const ready = objects().length && profiles().length && endpointList().length;
    const emptyHint = ready
      ? 'click <b>Add Connector</b> to decide what a device is polled for.'
      : `define a <a href="settings?tab=devices">device</a>, an
         <a href="settings?tab=api-key">API key</a> and an
         <a href="settings?tab=api-endpoint">API endpoint</a> first, then bind them here.`;

    const rows = state.connectors.length
      ? state.connectors.map((c, i) => `
        <tr class="${anyEnabled(c) ? '' : 'row-off'}">
          <td class="col-name">
            <div class="cell-name">${esc(c.name) || '<span class="muted">unnamed</span>'}</div>
            ${c.description ? `<div class="cell-sub">${esc(c.description)}</div>` : ''}
          </td>
          <td class="col-obj">${objectCell(c)}</td>
          <td class="col-authp">${profileCell(c)}</td>
          <td class="col-ep">${scheduleCell(c)}</td>
          <td class="col-act">
            <button class="icon-btn" data-edit="${i}" title="Edit">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
            </button>
            <button class="icon-btn danger" data-del="${i}" title="Delete">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
            </button>
          </td>
        </tr>`).join('')
      : `<tr><td colspan="5"><div class="cfg-empty">No API connectors yet — ${emptyHint}</div></td></tr>`;

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
            <th class="col-authp">API Key</th>
            <th class="col-ep">Collects</th>
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
  // No gated sequence: a connector is a schedule, and each row's API TEST proves that row
  // end-to-end (credential exchanged, then the endpoint called) whenever the operator wants to
  // check. Saving a schedule that has not been tested is allowed — an untested connector simply
  // reports its failures at collection time, which is where they would surface anyway.
  function itemRow(i) {
    const opts = ['<option value="">— select an endpoint —</option>'].concat(
      endpointList().map(e =>
        `<option value="${esc(e.oid)}" ${i.endpoint === e.oid ? 'selected' : ''}>${esc(e.name)}</option>`)
    ).join('');

    return `<div class="item-row">
        <div class="item-main">
          <select data-i="endpoint">${opts}</select>
          <input data-i="poll_interval_sec" class="item-int" value="${esc(i.poll_interval_sec)}"
                 title="Poll interval in seconds" placeholder="${DEFAULT_INTERVAL_SEC}"/>
          <label class="item-en" title="Collect this endpoint">
            <input type="checkbox" data-i="enabled" ${i.enabled ? 'checked' : ''}/><span>on</span>
          </label>
          <button class="btn-sm" data-item-test type="button"
                  title="Exchange the credential for a key and call this endpoint">API TEST</button>
          <button class="icon-btn danger" data-item-del type="button" title="Remove">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
          </button>
        </div>
        <div class="item-path">${esc(endpointPath(i.endpoint) || '—')}</div>
        <div class="item-result" data-item-result></div>
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

    return `
      <div class="field-row"><label>Name</label>
        <input data-f="name" value="${esc(c.name)}" placeholder="e.g. core-fw-01"/></div>
      <div class="field-row"><label>Description</label>
        <input data-f="description" value="${esc(c.description)}" placeholder="optional"/></div>

      <div class="editor-sec">TARGET &amp; CREDENTIAL</div>
      <div class="field-row"><label>Object</label>
        <select data-f="object">${objOpts}</select></div>
      <div class="field-row"><label>API Key</label>
        <select data-f="auth_profile">${profOpts}</select></div>

      <div class="editor-sec">COLLECTED ENDPOINTS</div>
      <p class="field-hint">Each endpoint is a complete request defined on the
        <a href="settings?tab=api-endpoint">API Endpoint</a> page. This decides which of them this
        device is polled for, and how often. <b>API TEST</b> issues a key and calls that one
        endpoint against this device.</p>
      <div class="item-head">
        <label>Endpoints</label>
        <button class="btn-sm" id="acItemAdd" type="button">+ Endpoint</button>
      </div>
      <div class="item-list" id="acItemList">${(c.items.length ? c.items : [normalizeItem({})])
        .map(itemRow).join('')}</div>`;
  }

  function collect(body) {
    const g = (k) => {
      const el = body.querySelector(`[data-f="${k}"]`);
      if (!el) return '';
      return el.type === 'checkbox' ? el.checked : el.value.trim();
    };

    const items = Array.from(body.querySelectorAll('.item-row')).map(row => ({
      endpoint: row.querySelector('[data-i="endpoint"]').value,
      poll_interval_sec: row.querySelector('[data-i="poll_interval_sec"]').value,
      enabled: row.querySelector('[data-i="enabled"]').checked,
    }));

    return normalize({
      oid: draftOid,
      name: g('name'),
      description: g('description'),
      object: g('object'),
      auth_profile: g('auth_profile'),
      items,
    });
  }

  // Save is blocked only by what makes a connector meaningless, and the reason is shown rather
  // than left for the operator to guess at a greyed-out button.
  function saveBlocker(c) {
    if (!c.object) return 'Pick the object this connector collects from.';
    if (!c.auth_profile) return 'Pick the API key used against it.';
    if (!c.items.length) return 'Add at least one endpoint to collect.';
    return '';
  }

  function refreshSave() {
    const body = document.getElementById('acBody');
    const saveBtn = document.getElementById('acSave');
    if (!body || !saveBtn) return;

    const reason = saveBlocker(collect(body));
    saveBtn.disabled = !!reason;
    saveBtn.title = reason;
  }

  function openEditor(idx) {
    editIdx = idx;
    const c = idx == null ? blank() : normalize(JSON.parse(JSON.stringify(state.connectors[idx])));
    draftOid = c.oid;

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

  function wireItemRow(row) {
    row.querySelector('[data-i="endpoint"]').addEventListener('change', (e) => {
      row.querySelector('.item-path').textContent = endpointPath(e.target.value) || '—';
      row.querySelector('[data-item-result]').innerHTML = '';
      refreshSave();
    });

    row.querySelector('[data-i="poll_interval_sec"]').addEventListener('input', refreshSave);
    row.querySelector('[data-i="enabled"]').addEventListener('change', refreshSave);

    row.querySelector('[data-item-del]').addEventListener('click', () => {
      row.remove();
      refreshSave();
    });

    row.querySelector('[data-item-test]').addEventListener('click', () => {
      const oid = row.querySelector('[data-i="endpoint"]').value;
      if (!oid) { alert('Pick an endpoint on this row first.'); return; }
      runApiTest(row, oid);
    });
  }

  function wireEditor() {
    const body = document.getElementById('acBody');

    body.querySelectorAll('[data-f]').forEach(el => {
      el.addEventListener(el.tagName === 'SELECT' ? 'change' : 'input', refreshSave);
    });
    body.querySelectorAll('.item-row').forEach(wireItemRow);

    document.getElementById('acItemAdd').onclick = () => {
      const list = document.getElementById('acItemList');
      list.insertAdjacentHTML('beforeend', itemRow(normalizeItem({})));
      wireItemRow(list.lastElementChild);
      list.lastElementChild.querySelector('[data-i="endpoint"]').focus();
      refreshSave();
    };

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
      const reason = saveBlocker(c);
      if (reason) { alert(reason); return; }
      if (editIdx == null) state.connectors.push(c); else state.connectors[editIdx] = c;
      stage();
      closeEditor(); render();
    };

    refreshSave();
  }

  // ── API test ────────────────────────────────────────────────────────────────
  // One button, one round trip: mgmtd hands the request to scand, which exchanges the
  // credential for a key and then calls the endpoint with it. The browser cannot reach a
  // customer's firewall, and neither daemon blocks its loop on a slow device — hence the ticket.
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

  // The device coordinates: host from the object, username and keygen path from the API Key
  // record, and the password from the browser-held store.
  function testPayload(c) {
    const o = objectByOid(c.object);
    const p = window.NMS.apiKeys ? window.NMS.apiKeys.byOid(c.auth_profile) : null;
    return {
      target: o ? o.target : '',
      fingerprint: o ? o.fingerprint : '',
      username: p ? p.username : '',
      keygen_endpoint: p ? (p.endpoint || '') : '',
      // Lets scand use the key already issued for this profile instead of asking for the
      // password again — see ApiService::issuedKey.
      api_key_oid: c.auth_profile,
      secrets: window.NMS.apiKeySecrets ? window.NMS.apiKeySecrets.for(c.auth_profile) : {},
    };
  }

  // Checked here rather than left to the backend, which can only answer "no username/password"
  // without knowing WHICH key or where it is kept.
  //
  // The password is held in sessionStorage, per browser tab, and is never committed — so it is
  // gone after the tab is closed, in a second tab, and after `pretzel reset` (the config version
  // goes backwards and every draft is dropped). The API Key record still shows a username, which
  // is why this reads as "the credentials are right there" when the test says otherwise.
  function credentialProblem(c) {
    const key = window.NMS.apiKeys ? window.NMS.apiKeys.byOid(c.auth_profile) : null;
    if (!key) return 'That API Key no longer exists — pick another.';

    const name = key.name || 'this API Key';
    if (!key.username) {
      return `API Key "${name}" has no username. Set it on the API Key page.`;
    }

    // Nothing else is checked here. Whether a key was already issued is scand's to know — it
    // holds the ones it could open from api_key_state — and second-guessing that in the browser
    // is how this ended up blocking a test that would have worked. `stored` is only a hint from
    // the last generation in this tab; its absence proves nothing.
    return '';
  }

  function stepRow(label, st) {
    const mark = !st ? '<span class="ts-idle">·</span>'
               : st.ok ? '<span class="ts-ok">✓</span>'
               : '<span class="ts-fail">✗</span>';
    return `<div class="ts-row"><span class="ts-mark">${mark}</span>
              <span class="ts-label">${label}</span>
              <span class="ts-detail">${esc((st && st.detail) || '')}</span></div>`;
  }

  // A device we have never pinned stops the test at the certificate: HttpClient will not put a
  // credential on the wire to an unverified peer. The operator confirms the fingerprint, it is
  // stored on the object, and the test is run again.
  function trustPrompt(res, c) {
    if (!res.fingerprint || res.fingerprint_trusted) return '';
    return `<div class="fp-prompt">
        <div class="fp-warn">Certificate is not trusted yet (self-signed)</div>
        <code class="fp-val">${esc(res.fingerprint)}</code>
        <div class="fp-sub">${esc(res.cert_subject || '')}</div>
        <button class="btn-sm btn-primary" data-trust-fp data-device="${esc(c.object)}"
                data-fp="${esc(res.fingerprint)}">Trust this certificate</button>
      </div>`;
  }

  // Pretty-print when the body is JSON; XML and error text are shown as returned.
  function formatBody(raw) {
    const text = String(raw == null ? '' : raw).trim();
    if (!text || (text[0] !== '{' && text[0] !== '[')) return text;
    try { return JSON.stringify(JSON.parse(text), null, 2); } catch (_) { return text; }
  }

  function showDetail(res) {
    if (!res || !window.NMS.modal) return;

    const req = res.request || {};
    const rsp = res.response || {};
    const statusClass = res.ok ? 'ok' : 'err';

    window.NMS.modal.open('API test', `
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

  async function runApiTest(row, endpointOid) {
    const body = document.getElementById('acBody');
    const c = collect(body);
    const out = row.querySelector('[data-item-result]');
    const btn = row.querySelector('[data-item-test]');

    const missing = !c.object ? 'Pick an object first.'
                  : (!c.auth_profile ? 'Pick an API key first.' : credentialProblem(c));
    if (missing) {
      out.innerHTML = `<div class="test-panel err"><div class="ts-note">${esc(missing)}</div></div>`;
      return;
    }

    btn.disabled = true;
    out.innerHTML = `<div class="test-panel running">
        ${stepRow('TLS connection', null)}${stepRow('API key generation', null)}${stepRow('Endpoint response', null)}
      </div>`;

    let res;
    try {
      // scand runs the test statelessly — the endpoint may not be committed yet, so the
      // reference is resolved here and the finished path is sent.
      const payload = testPayload(c);
      payload.api_type = endpointApiType(endpointOid);
      payload.endpoint = endpointPath(endpointOid);
      payload.params = [];   // an endpoint carries its own parameters
      res = await runDeviceTest('/api/connector/endpoint-test', payload);
    } catch (e) {
      btn.disabled = false;
      out.innerHTML = `<div class="test-panel err"><div class="ts-note">${esc(e.message)}</div></div>`;
      return;
    }

    btn.disabled = false;
    const steps = res.steps || {};
    out.innerHTML = `<div class="test-panel ${res.ok ? 'ok' : 'err'}">
        ${stepRow('TLS connection', steps.tls)}
        ${stepRow('API key generation', steps.auth)}
        ${stepRow('Endpoint response', steps.endpoint)}
        ${trustPrompt(res, c)}
        ${res.request ? `<div class="ts-more"><button class="btn-sm" data-detail type="button">View request / response</button></div>` : ''}
      </div>`;

    out.querySelector('[data-trust-fp]')?.addEventListener('click', (e) => {
      const b = e.currentTarget;
      window.NMS.devices.pinFingerprint(b.dataset.device, b.dataset.fp);
      b.outerHTML = '<span class="fp-trusted">Pinned — run the test again.</span>';
    });
    out.querySelector('[data-detail]')?.addEventListener('click', () => showDetail(res));

    // Raise the window straight away — the operator ran the test to see this.
    if (res.request) showDetail(res);
  }

  // ── Wiring (table) ──────────────────────────────────────────────────────────
  // render() replaces #contentBody wholesale, so row controls are re-bound on each paint.
  function wire() {
    const el = document.getElementById('contentBody');
    document.getElementById('acAdd')?.addEventListener('click', () => openEditor(null));
    el.querySelectorAll('[data-edit]').forEach(b =>
      b.addEventListener('click', () => openEditor(+b.dataset.edit)));
    el.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', () => {
      state.connectors.splice(+b.dataset.del, 1); stage(); render();
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

  // Object/profile/endpoint names come from the other tabs' data, which loads independently.
  document.addEventListener('nms:devices-ready', () => { if (activeTab() === 'api-connector') render(); });
  document.addEventListener('nms:api-keys-ready', () => { if (activeTab() === 'api-connector') render(); });
  document.addEventListener('nms:endpoints-ready', () => { if (activeTab() === 'api-connector') render(); });
})();
