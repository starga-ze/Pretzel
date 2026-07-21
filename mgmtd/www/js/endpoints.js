/* endpoints.js — Configuration ▸ API Profile ▸ API Endpoint.
 *
 * An API Endpoint is one call pretzel can make: the path and the query parameters, and nothing
 * about who to make it against. Keeping the device out is what makes it reusable — "fetch the
 * address objects" is written once and run against any customer's box.
 *
 * A test therefore names an API Key rather than a device: the key already carries the device it
 * was issued by, plus the credential to authenticate with. One choice supplies both.
 *
 * The release is part of the path (/restapi/v10.2/…) and pretzel does not rewrite it, so an
 * endpoint is implicitly scoped to the releases that serve that path — name it accordingly.
 *
 * Which API it speaks is read from the path rather than asked: PAN-OS serves the XML API under
 * /api/ and the REST API under /restapi/. That decides how the key is attached (query parameter
 * vs X-PAN-KEY header), so nothing is gained by making the operator restate it.
 *
 * Testing happens from the list, not the editor — the endpoint is a definition you keep and
 * re-verify, so the row carries its own Test action and Status.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // Read live (not once): settings tabs switch client-side without a page load (see main.js).
  const activeTab = () => new URLSearchParams(location.search).get('tab') || 'sites';
  const DRAFT_KEY = 'api_endpoints';

  const { esc, newUuid } = window.NMS.utils;

  // PAN-OS serves both APIs from fixed roots, so the path tells us which one this is.
  const apiTypeOf = (path) => (String(path || '').indexOf('/restapi/') === 0 ? 'rest' : 'xml');
  const API_LABEL = { rest: 'REST API', xml: 'XML API' };

  const PATH_HINT = '/restapi/v10.2/Objects/Addresses';
  const SEED_PARAMS = [{ name: 'location', value: 'vsys' }, { name: 'vsys', value: 'vsys1' }];

  // Test outcomes are runtime state, not part of the declaration, so they are held apart and
  // never stage the configuration. The database has api_endpoint_state for this.
  const STATE_KEY = 'api_endpoint_state';
  const testState = {
    for: (oid) => (window.NMS.draft.get(STATE_KEY, {})[oid] || null),
    put(oid, result) {
      const s = window.NMS.draft.get(STATE_KEY, {});
      s[oid] = result;
      window.NMS.draft.set(STATE_KEY, s);
    },
  };

  const state = { endpoints: [] };
  let deployed = [];
  let editIdx = null;
  let draftOid = null;

  function normalize(e) {
    return {
      oid: (typeof e.oid === 'string' && e.oid) ? e.oid : newUuid(),
      name: e.name || '',
      description: e.description || '',
      path: e.path || '',
      // Stored unencoded; percent-encoded when the URL is built, so raw values (an XML API
      // cmd=<show>… included) can be typed as-is.
      params: (Array.isArray(e.params) ? e.params : [])
        .map(p => ({ name: String((p && p.name) || ''), value: String((p && p.value) || '') }))
        .filter(p => p.name),
    };
  }

  const blank = () => normalize({ path: PATH_HINT, params: SEED_PARAMS });

  // The full path this endpoint calls. Encoding matches mgmtd's server-side build.
  function effectivePath(e) {
    let path = e.path || '';
    (e.params || []).forEach(p => {
      if (!p.name) return;
      path += (path.indexOf('?') === -1) ? '?' : '&';
      path += encodeURIComponent(p.name) + '=' + encodeURIComponent(p.value);
    });
    return path;
  }

  // ── Staging ──────────────────────────────────────────────────────────────────
  const stage = () => { window.NMS.draft.set(DRAFT_KEY, state.endpoints); refreshPending(); };
  const refreshPending = () => window.NMS.staging.refresh();

  // ── Data load ────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const r = await fetch('/api/settings', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();
      window.NMS.draft.checkBase(d.version);
      const api = ((d.daemons || {}).scand || {}).api || {};
      deployed = (Array.isArray(api.endpoints) ? api.endpoints : []).map(normalize);
    } catch (_) { deployed = []; }
    const staged = window.NMS.draft.get(DRAFT_KEY, null);
    state.endpoints = Array.isArray(staged) ? staged.map(normalize) : JSON.parse(JSON.stringify(deployed));
    refreshPending();
  }

  const commitPayload = () => [{ daemon: 'scand', domain: 'api', values: { endpoints: state.endpoints } }];

  window.NMS.staging.register({
    key: DRAFT_KEY,
    dirty: () => JSON.stringify(state.endpoints) !== JSON.stringify(deployed),
    payload: commitPayload,
    before: () => ({ endpoints: deployed }),
    after: () => ({ endpoints: state.endpoints }),
    onPublished() {
      deployed = JSON.parse(JSON.stringify(state.endpoints));
      window.NMS.draft.clear(DRAFT_KEY);
    },
  });

  // ── Cross-module surface ─────────────────────────────────────────────────────
  window.NMS.apiEndpoints = {
    list: () => state.endpoints.map(e => ({
      oid: e.oid, name: e.name, api_type: apiTypeOf(e.path), path: effectivePath(e),
    })),
    byOid: (oid) => state.endpoints.find(e => e.oid === oid) || null,
    label: (oid) => {
      const e = state.endpoints.find(x => x.oid === oid);
      return e ? e.name : null;
    },
  };

  // ── Render ───────────────────────────────────────────────────────────────────
  function statusCell(e) {
    const t = testState.for(e.oid);
    if (!t) return `<span class="st-never">never tested</span>`;
    const when = new Date(t.at).toLocaleString();
    // Which key it was proven against matters: the same path can pass on one release and 404 on
    // another, so the tooltip names it.
    const via = t.via ? ` via ${t.via}` : '';
    return t.ok
      ? `<span class="st-ok" title="HTTP ${esc(t.status || 200)}${esc(via)} — ${esc(when)}">OK</span>`
      : `<span class="st-fail" title="${esc(t.detail || '')}${esc(via)} — ${esc(when)}">failed</span>`;
  }

  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'api-endpoint') return;

    const rows = state.endpoints.length
      ? state.endpoints.map((e, i) => `
        <tr>
          <td class="col-name"><div class="cell-name">${esc(e.name) || '<span class="muted">unnamed</span>'}</div></td>
          <td class="col-desc">${esc(e.description) || '<span class="muted">—</span>'}</td>
          <td class="col-ep">
            <span class="api-badge ${esc(apiTypeOf(e.path))}">${esc(API_LABEL[apiTypeOf(e.path)])}</span>
            <span class="ep-path" title="${esc(effectivePath(e))}">${esc(effectivePath(e))}</span>
          </td>
          <td class="col-status">${statusCell(e)}</td>
          <td class="col-act">
            <button class="btn-sm" data-test="${i}">Endpoint Test</button>
            <button class="icon-btn" data-edit="${i}" title="Edit">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
            </button>
            <button class="icon-btn danger" data-del="${i}" title="Delete">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
            </button>
          </td>
        </tr>`).join('')
      : `<tr><td colspan="5"><div class="cfg-empty">No API endpoints yet — click <b>Add Endpoint</b> to define one.
           An endpoint is device-independent; a test names the API Key to run it against.</div></td></tr>`;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">API Endpoints</span>
            <span class="cfg-h-sub">${state.endpoints.length} endpoint${state.endpoints.length === 1 ? '' : 's'}</span>
          </div>
          <button class="btn-primary btn-sm" id="epAdd">+ Add Endpoint</button>
        </div>

        <table class="cfg-table cfg-table-endpoint">
          <thead><tr>
            <th class="col-name">Name</th>
            <th class="col-desc">Description</th>
            <th class="col-ep">Endpoint</th>
            <th class="col-status">Status</th>
            <th class="col-act"></th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>

        <p class="cfg-foot-note">Endpoints are reusable across devices. The release is part of the
          path and is entered as written (<code>/restapi/v10.2/…</code>), so an endpoint only fits
          the releases that serve it — say so in the name.</p>
      </div>

      <div class="slideover-overlay" id="epOverlay"></div>
      <aside class="slideover" id="epPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="epTitle">Add Endpoint</span>
          <button class="slideover-close" id="epClose">&times;</button>
        </div>
        <div class="slideover-body" id="epBody"></div>
        <div class="slideover-foot" id="epFoot"></div>
      </aside>`;

    wire();
  }

  // ── Editor ───────────────────────────────────────────────────────────────────
  function paramRow(p) {
    return `<div class="param-row">
        <input data-p="name" value="${esc(p.name)}" placeholder="name"/>
        <input data-p="value" value="${esc(p.value)}" placeholder="value"/>
        <button class="icon-btn danger" data-prm-del type="button" title="Remove">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        </button>
      </div>`;
  }

  function editorForm(e) {
    return `
      <div class="field-row"><label>Name</label>
        <input data-f="name" value="${esc(e.name)}" placeholder="e.g. address objects"/></div>
      <div class="field-row"><label>Description</label>
        <input data-f="description" value="${esc(e.description)}" placeholder="optional"/></div>

      <div class="editor-sec">CALL</div>
      <div class="field-row"><label>Endpoint</label>
        <input data-f="path" value="${esc(e.path)}" placeholder="${esc(PATH_HINT)}"/></div>
      <p class="field-hint" id="epApiHint"></p>

      <div class="param-head">
        <label>Query parameters</label>
        <button class="btn-sm" id="epParamAdd" type="button">+ Param</button>
      </div>
      <div class="param-list" id="epParamList">${(e.params.length ? e.params : [{ name: '', value: '' }])
        .map(paramRow).join('')}</div>
      <p class="field-hint">Values are percent-encoded for you — type them raw. The host and the
        API key come from whichever key the test names, so neither belongs here.</p>
      <div class="ep-preview"><span class="ep-preview-h">Calls</span>
        <code id="epPreview">${esc(effectivePath(e)) || '—'}</code></div>`;
  }

  function collect(body) {
    const g = (k) => {
      const el = body.querySelector(`[data-f="${k}"]`);
      return el ? el.value.trim() : '';
    };
    const params = Array.from(body.querySelectorAll('.param-row')).map(row => ({
      name: (row.querySelector('[data-p="name"]').value || '').trim(),
      value: (row.querySelector('[data-p="value"]').value || '').trim(),
    }));
    return normalize({
      oid: draftOid,
      name: g('name'),
      description: g('description'),
      path: g('path'),
      params,
    });
  }

  function refreshPreview() {
    const body = document.getElementById('epBody');
    const e = collect(body);
    const prev = document.getElementById('epPreview');
    if (prev) prev.textContent = effectivePath(e) || '—';
    const hint = document.getElementById('epApiHint');
    if (hint) {
      const t = apiTypeOf(e.path);
      hint.innerHTML = `Read as <b>${esc(API_LABEL[t])}</b> from the path — the key is attached
        automatically (${t === 'xml' ? '<code>key=</code> parameter' : '<code>X-PAN-KEY</code> header'}).`;
    }
  }

  function wireParamRow(row) {
    row.querySelectorAll('input').forEach(i => i.addEventListener('input', refreshPreview));
    row.querySelector('[data-prm-del]').addEventListener('click', () => { row.remove(); refreshPreview(); });
  }

  function openEditor(idx) {
    editIdx = idx;
    const e = idx == null ? blank() : normalize(JSON.parse(JSON.stringify(state.endpoints[idx])));
    draftOid = e.oid;
    document.getElementById('epTitle').textContent = idx == null ? 'Add Endpoint' : 'Edit Endpoint';
    document.getElementById('epBody').innerHTML = editorForm(e);
    document.getElementById('epFoot').innerHTML = `
      ${idx == null ? '' : '<button class="btn-sm btn-danger" id="epDelete">Delete</button>'}
      <span style="flex:1"></span>
      <button class="btn-sm" id="epCancel">Cancel</button>
      <button class="btn-primary btn-sm" id="epSave">Save</button>`;
    wireEditor();
    document.getElementById('epOverlay').classList.add('open');
    document.getElementById('epPanel').classList.add('open');
  }

  const closeEditor = () => {
    document.getElementById('epOverlay').classList.remove('open');
    document.getElementById('epPanel').classList.remove('open');
  };

  function usedByCount(oid) {
    const conns = (window.NMS.apiConnectors && window.NMS.apiConnectors()) || [];
    return conns.filter(c => c.api_endpoint === oid || (c.endpoints || []).some(x => x.endpoint === oid)).length;
  }

  function removeEndpoint(idx) {
    const e = state.endpoints[idx];
    const used = usedByCount(e.oid);
    if (used && !confirm(`${used} connector${used > 1 ? 's' : ''} still reference this endpoint. Delete anyway?`))
      return false;
    state.endpoints.splice(idx, 1);
    stage();
    return true;
  }

  function wireEditor() {
    const body = document.getElementById('epBody');

    body.querySelectorAll('[data-f]').forEach(el =>
      el.addEventListener(el.tagName === 'SELECT' ? 'change' : 'input', refreshPreview));
    body.querySelectorAll('.param-row').forEach(wireParamRow);

    document.getElementById('epParamAdd').onclick = () => {
      const list = document.getElementById('epParamList');
      list.insertAdjacentHTML('beforeend', paramRow({ name: '', value: '' }));
      wireParamRow(list.lastElementChild);
      list.lastElementChild.querySelector('[data-p="name"]').focus();
    };

    document.getElementById('epCancel').onclick = closeEditor;
    document.getElementById('epClose').onclick = closeEditor;
    document.getElementById('epOverlay').onclick = closeEditor;

    const del = document.getElementById('epDelete');
    if (del) del.onclick = () => { if (removeEndpoint(editIdx)) { closeEditor(); render(); } };

    document.getElementById('epSave').onclick = () => {
      const e = collect(body);
      if (!e.name) { alert('Name is required.'); return; }
      if (!e.path || e.path[0] !== '/') { alert('Endpoint must be a path starting with /'); return; }
      if (editIdx == null) state.endpoints.push(e); else state.endpoints[editIdx] = e;
      stage();
      closeEditor(); render();
    };

    refreshPreview();
  }

  // ── Endpoint test ───────────────────────────────────────────────────────────
  // Runs from the row. The browser cannot reach a customer's firewall, so mgmtd hands the call
  // to scand — the daemon that also polls these connectors on a schedule — and returns a ticket
  // we poll. 40 × 700ms bounds the wait above scand's own per-step timeout.
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

  function formatBody(raw) {
    const text = String(raw == null ? '' : raw).trim();
    if (!text || (text[0] !== '{' && text[0] !== '[')) return text;
    try { return JSON.stringify(JSON.parse(text), null, 2); } catch (_) { return text; }
  }

  // The endpoint is what is being debugged, so the outcome gets the whole window: the exact
  // request line on top, the full response below.
  function detailView(res, steps) {
    const req = res.request || {};
    const rsp = res.response || {};
    return `
      <div class="ep-detail">
        <div class="test-panel ${res.ok ? 'ok' : 'err'}">
          ${stepRow('TLS connection', steps.tls)}
          ${stepRow('API key generation', steps.auth)}
          ${stepRow('Endpoint response', steps.endpoint)}
        </div>
        ${req.url ? `<div class="ep-block">
          <div class="ep-block-h">Request</div>
          <pre class="ep-req">${esc(req.method || 'GET')} ${esc(req.url)}</pre>
          <div class="ep-note">API key sent via ${esc(req.key_delivery || '')} — redacted above.</div>
        </div>` : ''}
        ${rsp.status != null ? `<div class="ep-block">
          <div class="ep-block-h">Response
            <span class="ep-status ${res.ok ? 'ok' : 'err'}">HTTP ${esc(rsp.status)}</span>
            ${rsp.bytes != null ? `<span class="ep-bytes">${esc(rsp.bytes)} bytes${rsp.truncated ? ', truncated' : ''}</span>` : ''}
          </div>
          <pre class="ep-rsp">${esc(formatBody(rsp.body))}</pre>
        </div>` : ''}
      </div>`;
  }

  // An endpoint says what to call, not who to call it on — so the test names an API Key, and the
  // key supplies both the device (address, pinned certificate) and the credential. The same
  // endpoint can therefore be proven against several customers' boxes in turn.
  function openTestPicker(idx) {
    const e = state.endpoints[idx];
    const keys = (window.NMS.apiKeys && window.NMS.apiKeys.list()) || [];
    if (!keys.length) {
      alert('No API Key defined yet — add one under API Profile ▸ API Key first.');
      return;
    }

    const opts = keys.map(k => {
      const d = window.NMS.devices ? window.NMS.devices.byOid(k.device) : null;
      const where = d ? (d.name || d.target) : 'device missing';
      return `<option value="${esc(k.oid)}">${esc(k.name)} — ${esc(where)}</option>`;
    }).join('');

    window.NMS.modal.open('API Endpoint Test', `
      <p class="cm-lead">Runs <code>${esc(effectivePath(e))}</code> against the device the chosen
        key belongs to.</p>
      <div class="field-row"><label>API Key</label>
        <select id="epTestKey">${opts}</select></div>`,
      `<button class="btn-sm" id="cmDone">Cancel</button>
       <span style="flex:1"></span>
       <button class="btn-primary btn-sm" id="epRunTest">Run test</button>`);

    document.getElementById('epRunTest').onclick = () =>
      runEndpointTest(idx, document.getElementById('epTestKey').value);
  }

  async function runEndpointTest(idx, keyOid) {
    const e = state.endpoints[idx];
    const keyRecord = window.NMS.apiKeys ? window.NMS.apiKeys.byOid(keyOid) : null;
    if (!keyRecord) { alert('That API Key no longer exists.'); return; }

    const dev = window.NMS.devices ? window.NMS.devices.byOid(keyRecord.device) : null;
    if (!dev) { alert('The key references a device that no longer exists.'); return; }

    const held = window.NMS.apiKeySecrets ? window.NMS.apiKeySecrets.for(keyOid) : {};
    if (!held.password) { alert('That API Key has no password entered yet.'); return; }

    const modal = window.NMS.modal;
    modal.open('API Endpoint Test',
      `<div class="test-panel running">${stepRow('TLS connection', null)}${stepRow('API key generation', null)}${stepRow('Endpoint response', null)}</div>`);

    let res;
    try {
      res = await runDeviceTest('/api/connector/endpoint-test', {
        target: dev.target,
        fingerprint: dev.fingerprint,
        keygen_endpoint: keyRecord.endpoint,
        api_type: apiTypeOf(e.path),
        endpoint: e.path,
        params: e.params,
        secrets: { username: keyRecord.username, password: held.password },
      });
    } catch (err) {
      testState.put(e.oid, { at: Date.now(), ok: false, detail: err.message, via: keyRecord.name });
      render();
      modal.open('API Endpoint Test', `<div class="test-panel err"><div class="ts-note">${esc(err.message)}</div></div>`);
      return;
    }

    const steps = res.steps || {};
    testState.put(e.oid, {
      at: Date.now(), ok: !!res.ok,
      status: res.response ? res.response.status : 0,
      detail: res.message || '',
      via: keyRecord.name,
    });

    const trust = (res.fingerprint && !res.fingerprint_trusted)
      ? `<div class="fp-prompt">
           <div class="fp-warn">Certificate is not trusted yet (self-signed)</div>
           <code class="fp-val">${esc(res.fingerprint)}</code>
           <div class="fp-sub">${esc(res.cert_subject || '')}</div>
           <button class="btn-sm btn-primary" id="epTrustFp">Trust this certificate</button>
         </div>`
      : '';

    modal.open('API Endpoint Test', detailView(res, steps) + trust);

    document.getElementById('epTrustFp')?.addEventListener('click', (ev) => {
      window.NMS.devices.pinFingerprint(dev.oid, res.fingerprint);
      ev.currentTarget.outerHTML = '<span class="fp-trusted">Pinned to the device — run the test again.</span>';
    });

    render();
  }

  function wire() {
    const el = document.getElementById('contentBody');
    document.getElementById('epAdd')?.addEventListener('click', () => openEditor(null));
    el.querySelectorAll('[data-edit]').forEach(b =>
      b.addEventListener('click', () => openEditor(+b.dataset.edit)));
    el.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', () => {
      if (removeEndpoint(+b.dataset.del)) render();
    }));
    el.querySelectorAll('[data-test]').forEach(b =>
      b.addEventListener('click', () => openTestPicker(+b.dataset.test)));
  }

  // ── Init ─────────────────────────────────────────────────────────────────────
  const endpointRefresh = async () => { await load(); render(); };

  function activate() {
    render();
    window.NMS.onRefresh(endpointRefresh);
  }

  document.addEventListener('DOMContentLoaded', async () => {
    await load();
    if (activeTab() === 'api-endpoint') activate();
    document.dispatchEvent(new Event('nms:endpoints-ready'));
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === 'api-endpoint') activate();
  });

  document.addEventListener('nms:api-keys-ready', () => { if (activeTab() === 'api-endpoint') render(); });
  document.addEventListener('nms:connectors-ready', () => { if (activeTab() === 'api-endpoint') render(); });
})();
