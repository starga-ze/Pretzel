/* config.js — Configuration ▸ Site Management ▸ Devices.
 *
 * A Device is one managed box (or one cloud tenant) belonging to a Site. `device_type` is the
 * single axis that decides everything else:
 *
 *   ngfw           on-prem/VM firewall → reached at its own address    → Access Identifier = IP / FQDN
 *   sase           cloud platform      → reached via a tenant-scoped API → Access Identifier = Tenant ID
 *
 * Access Type is therefore derived, never stored twice and never asked of the operator.
 *
 * The device also carries `fingerprint`, the pinned certificate of its host. It is not an
 * operator-entered field: the first API Key test records it once the operator confirms it, and
 * every later call to this box is checked against it. Credentials do not live here — an API Key
 * (API Profile ▸ API Key) references a device and holds the account used against it.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // Read live (not once): settings tabs switch client-side without a page load (see main.js).
  const activeTab = () => new URLSearchParams(location.search).get('tab') || 'sites';
  const DRAFT_KEY = 'devices';

  const { esc, newUuid } = window.NMS.utils;

  // [key, label, access kind, access-identifier label, placeholder]
  const DEVICE_TYPES = [
    ['ngfw', 'NGFW', 'direct', 'IP / FQDN', '192.168.0.10'],
    ['sase', 'SASE', 'tenant', 'Tenant ID', '1963594622'],
  ];
  const typeRow = (t) => DEVICE_TYPES.find(([k]) => k === t) || DEVICE_TYPES[0];
  const typeLabel = (t) => typeRow(t)[1];
  const accessKind = (t) => typeRow(t)[2];
  const accessLabel = (t) => typeRow(t)[3];
  const accessPlaceholder = (t) => typeRow(t)[4];

  const state = { devices: [] };
  let deployed = [];
  let editIdx = null;
  let draftOid = null;   // survives the rebuild a device-type change performs

  // Staged edits are held in NMS.draft so they survive a full page reload.
  // Dirty state / publish / revert are owned by the cross-tab staging registry (js/commit.js).
  const stage = () => { window.NMS.draft.set(DRAFT_KEY, state.devices); refreshPending(); };
  const refreshPending = () => window.NMS.staging.refresh();

  function normalize(d) {
    // `type`/`platform` are the pre-consolidation keys: everything that was not a tenant
    // platform was an on-prem firewall, so it maps to ngfw.
    let deviceType = d.device_type;
    if (deviceType === 'prisma_access') deviceType = 'sase';   // pre-rename canonical value
    if (!DEVICE_TYPES.some(([k]) => k === deviceType)) {
      const legacyTenant = (d.platform || d.access) === 'tenant' || d.type === 'sase' || d.type === 'saas';
      deviceType = legacyTenant ? 'sase' : 'ngfw';
    }
    const out = {
      oid: (typeof d.oid === 'string' && d.oid) ? d.oid : (d.uuid || d.id || newUuid()),
      name: d.name || '',
      description: d.description || '',
      site: d.site || '',              // Site oid, '' = unassigned
      device_type: deviceType,
      target: d.target || '',          // access identifier: IP/FQDN or tenant id
      // Pinned certificate of this host — written by the API Key test, never typed.
      fingerprint: d.fingerprint || '',
    };
    // SASE control-plane health probe (getPrismaAccessIP): url + body live in config, and the api-key
    // rides alongside on the device. Only carried for SASE (or if set) so NGFW devices stay clean.
    const health = (d.health && typeof d.health === 'object')
      ? { url: d.health.url || '', body: d.health.body || '' } : { url: '', body: '' };
    const apiKey = d.api_key || '';
    if (deviceType === 'sase' || health.url || apiKey) {
      out.health = health;
      out.api_key = apiKey;
    }
    return out;
  }

  const blank = () => normalize({ device_type: 'ngfw' });

  // ── Data load ────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const r = await fetch('/api/settings', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();
      window.NMS.draft.checkBase(d.version);
      const site = ((d.daemons || {}).engined || {}).site || {};
      // Config stores two type-specific arrays; the editor works on one merged list tagged with
      // device_type, and splitForConfig() puts them back on commit.
      const ngfw = (Array.isArray(site.ngfw_devices) ? site.ngfw_devices : [])
        .map(x => normalize(Object.assign({}, x, { device_type: 'ngfw' })));
      const sase = (Array.isArray(site.sase_devices) ? site.sase_devices : [])
        .map(x => normalize(Object.assign({}, x, { device_type: 'sase' })));
      deployed = ngfw.concat(sase);
    } catch (_) { deployed = []; }
    const staged = window.NMS.draft.get(DRAFT_KEY, null);
    state.devices = Array.isArray(staged) ? staged.map(normalize)
                                          : JSON.parse(JSON.stringify(deployed));
    refreshPending();
  }

  // Split the merged editor list back into the two config arrays. device_type is the tag (dropped),
  // and the SASE api-key never goes in config — it is persisted to the DB via its own endpoint.
  function splitForConfig(list) {
    const ngfw_devices = [], sase_devices = [];
    list.forEach(function (d) {
      const base = { oid: d.oid, name: d.name, description: d.description, site: d.site, target: d.target };
      if (d.device_type === 'sase') sase_devices.push(Object.assign(base, { health: d.health || { url: '', body: '' } }));
      else ngfw_devices.push(Object.assign(base, { fingerprint: d.fingerprint || '' }));
    });
    return { ngfw_devices: ngfw_devices, sase_devices: sase_devices };
  }

  const commitPayload = () => [{ daemon: 'engined', domain: 'site', values: splitForConfig(state.devices) }];

  // Site is a reference; show the site's name, and flag a dangling oid so a deleted site is
  // visible rather than silently blank.
  function siteCell(d) {
    if (!d.site) return `<span class="muted">—</span>`;
    const name = window.NMS.sites && window.NMS.sites.label(d.site);
    return name
      ? `<span class="site-chip">${esc(name)}</span>`
      : `<span class="ref-missing" title="Site ${esc(d.site)} no longer exists">missing</span>`;
  }

  // ── Render ─────────────────────────────────────────────────────────────────────
  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'devices') return;

    const rows = state.devices.length
      ? state.devices.map((d, i) => `
        <tr>
          <td class="col-name"><div class="cell-name">${esc(d.name) || '<span class="muted">—</span>'}</div></td>
          <td class="col-desc">${esc(d.description) || '<span class="muted">—</span>'}</td>
          <td class="col-site">${siteCell(d)}</td>
          <td class="col-type"><span class="type-badge">${esc(typeLabel(d.device_type))}</span></td>
          <td class="col-access"><span class="access-badge ${esc(accessKind(d.device_type))}">${esc(accessLabel(d.device_type))}</span></td>
          <td class="col-endpoint">${esc(d.target) || '<span class="muted">—</span>'}</td>
          <td class="col-act">
            <button class="icon-btn" data-edit="${i}" title="Edit">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
            </button>
            <button class="icon-btn danger" data-del="${i}" title="Delete">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
            </button>
          </td>
        </tr>`).join('')
      : `<tr><td colspan="7"><div class="cfg-empty">No devices yet — click <b>Add Device</b> to define one.
           ${(window.NMS.sites && window.NMS.sites.list().length) ? ''
             : 'Tip: <a href="settings?tab=sites">create a site first</a> so devices can belong to one.'}</div></td></tr>`;

    const counts = DEVICE_TYPES.map(([k, label]) =>
      `${state.devices.filter(d => d.device_type === k).length} ${label}`).join(' · ');

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">Devices</span>
            <span class="cfg-h-sub">${counts}</span>
          </div>
          <button class="btn-primary btn-sm" id="devAdd">+ Add Device</button>
        </div>

        <table class="cfg-table cfg-table-device">
          <thead><tr>
            <th class="col-name">Name</th>
            <th class="col-desc">Description</th>
            <th class="col-site">Site</th>
            <th class="col-type">Device Type</th>
            <th class="col-access">Access Type</th>
            <th class="col-endpoint">Access Identifier</th>
            <th class="col-act"></th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>

      <div class="slideover-overlay" id="devOverlay"></div>
      <aside class="slideover" id="devPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="devTitle">Add Device</span>
          <button class="slideover-close" id="devClose">&times;</button>
        </div>
        <div class="slideover-body" id="devBody"></div>
        <div class="slideover-foot" id="devFoot"></div>
      </aside>`;

    wire();
  }

  // ── Editor (slide-over) ─────────────────────────────────────────────────────
  function fieldRow(label, key, val, ph) {
    return `<div class="field-row"><label>${esc(label)}</label>
      <input data-f="${esc(key)}" value="${esc(val)}" placeholder="${esc(ph || '')}"/></div>`;
  }

  function editorForm(d) {
    const typeOpts = DEVICE_TYPES.map(([k, label]) =>
      `<option value="${esc(k)}" ${d.device_type === k ? 'selected' : ''}>${esc(label)}</option>`).join('');

    const sites = (window.NMS.sites && window.NMS.sites.list()) || [];
    const siteOpts = ['<option value="">— none —</option>'].concat(
      sites.map(s => `<option value="${esc(s.oid)}" ${d.site === s.oid ? 'selected' : ''}>${esc(s.name)}</option>`)
    ).join('');

    return `
      ${fieldRow('Name', 'name', d.name, 'e.g. core-fw-01')}
      ${fieldRow('Description', 'description', d.description, 'optional')}
      <div class="field-row"><label>Site</label>
        <select data-f="site">${siteOpts}</select></div>
      ${sites.length ? '' :
        `<p class="field-hint"><a href="settings?tab=sites">Create a site first</a> to place this device.</p>`}

      <div class="editor-sec">ACCESS</div>
      <div class="field-row"><label>Device Type</label>
        <select data-f="device_type" data-typesel>${typeOpts}</select></div>
      <div class="field-row"><label>Access Type</label>
        <input value="${esc(accessLabel(d.device_type))}" disabled/></div>
      <p class="field-hint">Access Type follows the device type — an NGFW is reached at its own
        address, SASE through its tenant.</p>
      ${fieldRow(accessLabel(d.device_type), 'target', d.target, accessPlaceholder(d.device_type))}
      ${d.fingerprint
        ? `<div class="fp-box"><div class="fp-label">Pinned certificate (SHA-256)</div>
             <code class="fp-val">${esc(d.fingerprint)}</code>
             <button class="btn-sm btn-danger" id="devUnpin" type="button">Clear pin</button></div>`
        : ''}
      ${d.device_type === 'sase' ? healthSection(d.health || {}, d.api_key || '') : ''}`;
  }

  // SASE has no IP to ping, so its reachability is a control-plane API read (getPrismaAccessIP).
  // The operator pastes the whole curl command; Parse pulls the URL, api-key and body into the
  // fields below, which is what actually gets saved. probed runs this on a schedule and sets the
  // device active/down from the outcome.
  function healthSection(health, apiKey) {
    return `
      <div class="editor-sec">SASE HEALTH PROBE</div>
      <p class="field-hint">SASE has no address to ping — reachability is a live
        <code>getPrismaAccessIP</code> read against the tenant's control plane. The api-key is sealed
        and stored on the appliance, never in the configuration.</p>
      ${fieldRow('Probe URL', 'health_url', health.url || '', 'https://api.prodN.datapath.prismaaccess.com/getPrismaAccessIP/v2')}
      <div class="field-row"><label>API key</label>
        <input data-f="api_key" type="password" value="${esc(apiKey)}" autocomplete="off"
               placeholder="${apiKey ? '•••••••• entered' : 'header-api-key value'}"/></div>
      ${fieldRow('Request body', 'health_body', health.body || '', '{"serviceType":"all","addrType":"all","location":"all"}')}
      <details class="dev-paste" style="margin:2px 0 4px">
        <summary style="cursor:pointer;color:var(--text-accent,#2a6);font-size:13px;user-select:none">Paste a curl command to autofill</summary>
        <textarea data-f="paste_cmd" rows="3"
          style="width:100%;box-sizing:border-box;margin-top:8px;font-family:var(--font-mono,monospace);font-size:12px;resize:vertical"
          placeholder="curl -X POST -d @option.txt -H &quot;header-api-key: ...&quot; https://api.prod8.datapath.prismaaccess.com/getPrismaAccessIP/v2"></textarea>
        <button class="btn-sm" id="devParseCmd" type="button" style="margin-top:6px">Autofill from command</button>
      </details>
      <div class="field-row" style="margin-top:6px"><label></label>
        <button class="btn-sm" id="devSaseTest" type="button">Test &amp; save api-key</button></div>
      <div id="devSaseTestResult" class="field-hint" style="min-height:18px"></div>`;
  }

  // Validate the getPrismaAccessIP api-key against the tenant; on success probed seals + stores it in
  // sase_device.api_key_enc. Same ticket/poll shape as the API Key tests.
  async function runSaseTest(payload) {
    const start = await fetch('/api/connector/sase-test', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    if (start.status === 401) { location.href = '/'; return { ok: false, message: 'session expired' }; }
    const started = await start.json().catch(() => null);
    if (!start.ok || !started || !started.ticket) throw new Error((started && started.error) || 'could not start the test');
    for (let i = 0; i < 30; i++) {
      await new Promise(r => setTimeout(r, 1000));
      const r = await fetch('/api/connector/test-result?ticket=' + started.ticket,
        { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      const d = await r.json().catch(() => null);
      if (d && d.status === 'done') return d;
    }
    throw new Error('the test timed out');
  }

  // Pull the URL, header-api-key and inline body out of a pasted curl command. A `-d @file` body
  // (which we cannot read) falls back to the all/all/all default.
  function parsePrismaCommand(cmd) {
    const url = (cmd.match(/https:\/\/[^\s"']+/) || [''])[0];
    const apiKey = (cmd.match(/header-api-key:\s*([^\s"']+)/i) || [, ''])[1];
    let body = '';
    const m = cmd.match(/-d\s+'([^']*)'/) || cmd.match(/-d\s+"([^"]*)"/) || cmd.match(/--data(?:-raw)?\s+'([^']*)'/);
    if (m) body = m[1];
    else if (/-d\s+@/.test(cmd)) body = '{"serviceType":"all","addrType":"all","location":"all"}';
    return { url, apiKey, body };
  }

  // `oid` is carried out-of-band (draftOid) so it survives the device-type rebuild and stays
  // immutable for the device's lifetime.
  function collectForm(body) {
    const g = (k) => {
      const e = body.querySelector(`[data-f="${k}"]`);
      return e ? (e.type === 'checkbox' ? e.checked : e.value.trim()) : undefined;
    };
    const prev = editIdx == null ? null : state.devices[editIdx];
    return normalize({
      oid: draftOid,
      name: g('name'),
      description: g('description'),
      site: g('site'),
      device_type: g('device_type'),
      target: g('target'),
      fingerprint: prev ? prev.fingerprint : '',
      // Present only on the SASE form; undefined on NGFW so normalize drops them.
      health: (g('health_url') !== undefined || g('health_body') !== undefined)
        ? { url: g('health_url') || '', body: g('health_body') || '' } : (prev ? prev.health : undefined),
      api_key: g('api_key') !== undefined ? g('api_key') : (prev ? prev.api_key : undefined),
    });
  }

  function openEditor(idx) {
    editIdx = idx;
    const d = idx == null ? blank() : normalize(JSON.parse(JSON.stringify(state.devices[idx])));
    draftOid = d.oid;
    document.getElementById('devTitle').textContent = idx == null ? 'Add Device' : 'Edit Device';
    document.getElementById('devBody').innerHTML = editorForm(d);
    document.getElementById('devFoot').innerHTML = `
      ${idx == null ? '' : '<button class="btn-sm btn-danger" id="devDelete">Delete</button>'}
      <span style="flex:1"></span>
      <button class="btn-sm" id="devCancel">Cancel</button>
      <button class="btn-primary btn-sm" id="devSave">Save</button>`;
    wireEditor();
    document.getElementById('devOverlay').classList.add('open');
    document.getElementById('devPanel').classList.add('open');
  }

  const closeEditor = () => {
    document.getElementById('devOverlay').classList.remove('open');
    document.getElementById('devPanel').classList.remove('open');
  };

  // Anything referencing this device becomes dangling, so say so rather than silently orphaning.
  function referenceCount(oid) {
    const keys = (window.NMS.apiKeys && window.NMS.apiKeys.list()) || [];
    return keys.filter(k => k.device === oid).length;
  }

  function removeDevice(idx) {
    const d = state.devices[idx];
    const used = referenceCount(d.oid);
    if (used && !confirm(`${used} API key${used > 1 ? 's' : ''} reference this device. Delete anyway?`))
      return false;
    state.devices.splice(idx, 1);
    stage();
    return true;
  }

  function wireEditor() {
    const body = document.getElementById('devBody');

    // Device type drives the access labels and placeholder, so switching rebuilds the form —
    // entered values and the oid survive through collectForm.
    body.querySelector('[data-typesel]')?.addEventListener('change', () => {
      body.innerHTML = editorForm(collectForm(body));
      wireEditor();
    });

    document.getElementById('devUnpin')?.addEventListener('click', () => {
      const d = collectForm(body);
      d.fingerprint = '';
      if (editIdx != null) state.devices[editIdx].fingerprint = '';
      body.innerHTML = editorForm(d);
      wireEditor();
      stage();
    });

    document.getElementById('devParseCmd')?.addEventListener('click', () => {
      const d = collectForm(body);
      const cmd = body.querySelector('[data-f="paste_cmd"]')?.value || '';
      const p = parsePrismaCommand(cmd);
      if (!p.url && !p.apiKey) { alert('Could not find a URL or api-key in that command.'); return; }
      d.health = { url: p.url || (d.health && d.health.url) || '', body: p.body || (d.health && d.health.body) || '' };
      d.api_key = p.apiKey || d.api_key || '';
      body.innerHTML = editorForm(d);
      wireEditor();
    });

    document.getElementById('devSaseTest')?.addEventListener('click', async () => {
      const d = collectForm(body);
      const out = document.getElementById('devSaseTestResult');
      if (!(d.health && d.health.url) || !d.api_key) {
        out.innerHTML = '<span style="color:var(--text-danger,#c33)">Enter the probe URL and api-key first.</span>'; return;
      }
      out.textContent = 'Testing… (this can take several seconds)';
      const btn = document.getElementById('devSaseTest');
      if (btn) btn.disabled = true;
      try {
        const res = await runSaseTest({ oid: d.oid, url: d.health.url, body: d.health.body, api_key: d.api_key });
        out.innerHTML = res.ok
          ? '<span style="color:var(--text-success,#2a6)">✓ ' + esc(res.message || 'passed') + '</span>'
          : '<span style="color:var(--text-danger,#c33)">✕ ' + esc(res.message || 'failed') + '</span>';
      } catch (e) {
        out.innerHTML = '<span style="color:var(--text-danger,#c33)">✕ ' + esc(e.message || 'test failed') + '</span>';
      } finally {
        if (btn) btn.disabled = false;
      }
    });

    document.getElementById('devCancel').onclick = closeEditor;
    document.getElementById('devClose').onclick = closeEditor;
    document.getElementById('devOverlay').onclick = closeEditor;

    const del = document.getElementById('devDelete');
    if (del) del.onclick = () => { if (removeDevice(editIdx)) { closeEditor(); render(); } };

    document.getElementById('devSave').onclick = () => {
      const d = collectForm(body);
      if (!d.name) { alert('Device name is required.'); return; }
      if (!d.target) { alert(`${accessLabel(d.device_type)} is required.`); return; }
      if (editIdx == null) state.devices.push(d); else state.devices[editIdx] = d;
      closeEditor(); stage(); render();
    };
  }

  // ── Wiring (table) ────────────────────────────────────────────────────────────
  function wire() {
    const el = document.getElementById('contentBody');
    document.getElementById('devAdd')?.addEventListener('click', () => openEditor(null));
    el.querySelectorAll('[data-edit]').forEach(b =>
      b.addEventListener('click', () => openEditor(+b.dataset.edit)));
    el.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', () => {
      if (removeDevice(+b.dataset.del)) render();
    }));
  }

  // ── Staging provider ────────────────────────────────────────────────────────
  window.NMS.staging.register({
    key: DRAFT_KEY,
    dirty: () => JSON.stringify(state.devices) !== JSON.stringify(deployed),
    payload: commitPayload,
    before: () => splitForConfig(deployed),
    after: () => splitForConfig(state.devices),
    onPublished() {
      deployed = JSON.parse(JSON.stringify(state.devices));
      window.NMS.draft.clear(DRAFT_KEY);
    },
    problems() {
      const sites = (window.NMS.sites && window.NMS.sites.list()) || [];
      return state.devices
        .filter(d => d.site && !sites.some(s => s.oid === d.site))
        .map(d => `Device "${d.name || d.target}" belongs to a site that does not exist.`);
    },
  });

  // ── Cross-module surface ────────────────────────────────────────────────────
  // API keys, endpoints and connectors all resolve a device from here.
  window.NMS.devices = {
    list: () => state.devices.map(d => ({
      oid: d.oid, name: d.name, site: d.site, device_type: d.device_type,
      target: d.target, fingerprint: d.fingerprint,
    })),
    byOid: (oid) => state.devices.find(d => d.oid === oid) || null,
    label: (oid) => {
      const d = state.devices.find(x => x.oid === oid);
      return d ? (d.name || d.target) : null;
    },
    typeLabel,
    // Trust-on-first-use: the certificate belongs to the host, so the pin is stored here and
    // shared by every API key and endpoint that talks to this device.
    pinFingerprint(oid, fingerprint) {
      const d = state.devices.find(x => x.oid === oid);
      if (!d || !fingerprint) return;
      d.fingerprint = fingerprint;
      stage();
    },
  };

  // ── Init ────────────────────────────────────────────────────────────────────
  const devicesRefresh = async () => { await load(); render(); };

  function activate() {
    render();
    window.NMS.onRefresh(devicesRefresh);
  }

  // Data loads on every Configuration tab — the registry needs this domain's dirty state even
  // when another tab is showing — but rendering stays devices-only.
  document.addEventListener('DOMContentLoaded', async () => {
    if (activeTab() === 'devices') render();   // skeleton before the data lands
    await load();
    if (activeTab() === 'devices') activate();
    document.dispatchEvent(new Event('nms:devices-ready'));
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === 'devices') activate();
  });

  // Site names come from the Sites tab, which loads independently.
  document.addEventListener('nms:sites-ready', () => { if (activeTab() === 'devices') render(); });
})();
