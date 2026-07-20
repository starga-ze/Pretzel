/* config.js — Configuration ▸ Inventory. Renders the managed-object table into #contentBody.
 *
 * An object is either:
 *   • on-prem — identified by IP, reachability probed by ICMP, collected via SNMP/API
 *   • cloud   — a SASE firewall or SaaS service, identified by provider + account (NOT IP),
 *               monitored via its API (no ICMP)
 *
 * Excluded IPs are predefined (localhost). Publish opens a review modal with a side-by-side
 * (before | after) JSON diff, then a progress bar while the reload converges.
 */
(function () {
  'use strict';

  const TAB = new URLSearchParams(location.search).get('tab') || 'inventory';
  const EXCLUDED_IPS = ['127.0.0.1'];
  const STATUS_POLL_MS = 5000;

  const state = { objects: [] };   // [{ type, ip, provider, account, description, enabled, snmp, api }]
  let deployed = { excluded: '', objects: [] };
  let editIdx = null;
  let liveAlive = new Set();
  let statusReady = false;
  let pending = new Set();
  let statusTimer = null;

  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"]/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  function refreshPending() {
    window.NMS.setPendingChanges(JSON.stringify(state.objects) !== JSON.stringify(deployed.objects) ? 1 : 0);
  }

  // ── Data load ────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const r = await fetch('/api/settings', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();
      const probe = ((d.daemons || {}).icmpd || {}).probe || {};
      state.objects = (Array.isArray(probe.probe_targets) ? probe.probe_targets : []).map(normalize);
      deployed = { excluded: String(probe.excluded_ips || ''), objects: JSON.parse(JSON.stringify(state.objects)) };
    } catch (_) { /* leave empty on failure */ }
    window.NMS.setPendingChanges(0);
  }

  function normalize(t) {
    return {
      type: t.type === 'cloud' ? 'cloud' : 'on-prem',
      ip: t.ip || '',
      provider: t.provider || '',
      account: t.account || '',
      description: t.description || '',
      enabled: t.enabled !== false,
      snmp: Object.assign({ on: false, version: 'v2c', community: 'public', port: 161,
                            user: '', auth_pass: '', priv_pass: '' }, t.snmp),
      api:  Object.assign({ on: false, endpoint: '', token: '' }, t.api),
    };
  }

  function blankObject() {
    return normalize({ type: 'on-prem', enabled: true });
  }

  function commitPayload() {
    return [{
      daemon: 'icmpd', domain: 'probe',
      values: { excluded_ips: EXCLUDED_IPS.join(','), probe_targets: state.objects },
    }];
  }

  // ── Live status ──────────────────────────────────────────────────────────────
  async function pollStatus() {
    try {
      const d = await fetch('/api/inventory/status', { credentials: 'same-origin', headers: { Accept: 'application/json' } })
        .then(r => (r.ok ? r.json() : null));
      if (d && Array.isArray(d.alive)) {
        liveAlive = new Set(d.alive);
        statusReady = true;
        pending.clear();
        paintStatus();
      }
    } catch (_) { /* keep last known */ }
  }

  function startStatusPolling() {
    if (statusTimer) return;
    pollStatus();
    statusTimer = setInterval(pollStatus, STATUS_POLL_MS);
  }

  // on-prem: active/down/pending via ICMP · cloud: 'cloud' (API-monitored) · off: disabled
  function statusOf(o) {
    if (!o.enabled) return 'off';
    if (o.type === 'cloud') return 'cloud';
    if (pending.has(o.ip) || !statusReady) return 'pending';
    return liveAlive.has(o.ip) ? 'active' : 'down';
  }

  function paintStatus() {
    document.querySelectorAll('[data-st-ip]').forEach(dot => {
      const o = state.objects.find(x => (x.ip || x.provider) === dot.dataset.stIp);
      const s = o ? statusOf(o) : 'off';
      dot.className = 'st-dot ' + s;
      const badge = dot.closest('.st-badge');
      if (badge) { badge.title = STATUS_TIP[s]; const t = badge.querySelector('.st-txt'); if (t) t.textContent = STATUS_LABEL[s]; }
    });
  }
  const STATUS_LABEL = { active: 'Active', down: 'Down', pending: 'Pending', off: 'Disabled', cloud: 'API' };
  const STATUS_TIP   = { active: 'ICMP responding', down: 'No ICMP response', pending: 'Awaiting first probe result',
                         off: 'Object disabled — not monitored', cloud: 'Cloud object — monitored via API' };

  // ── Render ─────────────────────────────────────────────────────────────────────
  function methodPills(o) {
    const p = (on, name) => `<span class="m-pill ${on ? 'on' : 'off'}">${name}</span>`;
    if (o.type === 'cloud') return p(o.api && o.api.on, 'API');
    return p(o.snmp && o.snmp.on, 'SNMP') + p(o.api && o.api.on, 'API');
  }

  function addressOf(o) {
    if (o.type === 'cloud')
      return o.provider ? `${esc(o.provider)}${o.account ? ' · ' + esc(o.account) : ''}` : '<span class="muted">—</span>';
    return esc(o.ip) || '<span class="muted">—</span>';
  }

  function render() {
    const el = document.getElementById('contentBody');
    if (!el) return;

    if (TAB !== 'inventory') { el.innerHTML = `<div class="cfg-empty">Nothing here yet.</div>`; return; }

    const rows = state.objects.length
      ? state.objects.map((o, i) => `
        <tr class="${o.enabled ? '' : 'row-off'}">
          <td class="col-st">
            <span class="st-badge" title="${STATUS_TIP[statusOf(o)]}">
              <span class="st-dot ${statusOf(o)}" data-st-ip="${esc(o.ip || o.provider)}"></span>
              <span class="st-txt">${STATUS_LABEL[statusOf(o)]}</span>
            </span>
          </td>
          <td class="col-type"><span class="type-badge ${o.type === 'cloud' ? 'cloud' : 'onprem'}">${o.type === 'cloud' ? 'Cloud' : 'On-prem'}</span></td>
          <td class="col-addr">${addressOf(o)}</td>
          <td class="col-desc">${esc(o.description) || '<span class="muted">—</span>'}</td>
          <td class="col-method">${methodPills(o)}</td>
          <td class="col-en">
            <label class="tgl" title="${o.enabled ? 'Enabled' : 'Disabled'}">
              <input type="checkbox" data-toggle="${i}" ${o.enabled ? 'checked' : ''}/>
              <span class="tgl-track"></span>
            </label>
          </td>
          <td class="col-act">
            <button class="icon-btn" data-edit-btn="${i}" title="Edit">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
            </button>
            <button class="icon-btn danger" data-del="${i}" title="Delete">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
            </button>
          </td>
        </tr>`).join('')
      : `<tr><td colspan="7"><div class="cfg-empty">No objects yet — click <b>Add Object</b> to define one.</div></td></tr>`;

    const onprem = state.objects.filter(o => o.type !== 'cloud').length;
    const cloud  = state.objects.length - onprem;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">Inventory</span>
            <span class="cfg-h-sub">${onprem} on-prem · ${cloud} cloud · excluded: ${EXCLUDED_IPS.join(', ')}</span>
          </div>
          <button class="btn-primary btn-sm" id="cfgAdd">+ Add Object</button>
        </div>

        <table class="cfg-table cfg-table-inv">
          <thead><tr>
            <th class="col-st">Status</th>
            <th class="col-type">Type</th>
            <th class="col-addr">Address</th>
            <th class="col-desc">Description</th>
            <th class="col-method">Method</th>
            <th class="col-en">Enable</th>
            <th class="col-act"></th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>

        <div class="st-legend">
          <span><i class="st-dot active"></i>Active</span>
          <span><i class="st-dot down"></i>No response</span>
          <span><i class="st-dot pending"></i>Pending</span>
          <span><i class="st-dot cloud"></i>Cloud (API)</span>
          <span><i class="st-dot off"></i>Disabled</span>
        </div>
      </div>

      <div class="slideover-overlay" id="soOverlay"></div>
      <aside class="slideover" id="soPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="soTitle">Add Object</span>
          <button class="slideover-close" id="soClose">&times;</button>
        </div>
        <div class="slideover-body" id="soBody"></div>
        <div class="slideover-foot" id="soFoot"></div>
      </aside>`;

    wire();
  }

  // ── Editor (slide-over) ─────────────────────────────────────────────────────
  function openEditor(idx) {
    editIdx = idx;
    const o = idx == null ? blankObject() : normalize(JSON.parse(JSON.stringify(state.objects[idx])));
    document.getElementById('soTitle').textContent = idx == null ? 'Add Object' : 'Edit Object';
    document.getElementById('soBody').innerHTML = editorForm(o);
    document.getElementById('soFoot').innerHTML = `
      ${idx == null ? '' : '<button class="btn-sm btn-danger" id="soDelete">Delete</button>'}
      <span style="flex:1"></span>
      <button class="btn-sm" id="soCancel">Cancel</button>
      <button class="btn-primary btn-sm" id="soSave">Save</button>`;
    wireEditor();
    document.getElementById('soOverlay').classList.add('open');
    document.getElementById('soPanel').classList.add('open');
  }
  function closeEditor() {
    document.getElementById('soOverlay').classList.remove('open');
    document.getElementById('soPanel').classList.remove('open');
  }

  function methodBlock(id, name, checked, detail) {
    return `
      <div class="method-block ${checked ? 'on' : ''}" data-mblock="${id}">
        <label class="method-head"><input type="checkbox" data-mcheck="${id}" ${checked ? 'checked' : ''}/><span class="method-name">${name}</span></label>
        <div class="method-detail">${detail}</div>
      </div>`;
  }

  function fieldRow(label, id, val, ph) {
    return `<div class="field-row"><label>${label}</label><input data-f="${id}" value="${esc(val)}" placeholder="${esc(ph || '')}"/></div>`;
  }

  function editorForm(o) {
    const f = fieldRow;

    const common = `
      <div class="field-row"><label>Type</label>
        <select data-f="type" data-typesel>
          <option value="on-prem" ${o.type !== 'cloud' ? 'selected' : ''}>On-prem (IP-reachable device)</option>
          <option value="cloud"   ${o.type === 'cloud' ? 'selected' : ''}>Cloud (SASE / SaaS service)</option>
        </select></div>
      ${f('Description', 'description', o.description, 'e.g. core switch')}
      <label class="field-check"><input type="checkbox" data-f="enabled" ${o.enabled ? 'checked' : ''}/><span>Enabled</span></label>`;

    if (o.type === 'cloud') {
      return common + `
        <div class="editor-sec">IDENTITY</div>
        ${f('Provider', 'provider', o.provider, 'Prisma Access / Zscaler / Microsoft 365')}
        ${f('Account / Tenant', 'account', o.account, 'e.g. acme-1234')}
        <div class="editor-sec">API</div>
        ${f('Endpoint', 'api.endpoint', o.api.endpoint, 'https://api.vendor.com')}
        ${f('Token', 'api.token', o.api.token)}
        <p class="editor-note">Cloud objects are identified by <b>provider + account</b> (not IP) and monitored via their API — no ICMP.</p>`;
    }

    const snmp = `
      <div class="field-row"><label>Version</label>
        <select data-f="snmp.version" data-snmpver>
          <option value="v2c" ${o.snmp.version === 'v2c' ? 'selected' : ''}>SNMP v2c</option>
          <option value="v3"  ${o.snmp.version === 'v3'  ? 'selected' : ''}>SNMP v3</option>
        </select></div>
      <div data-snmpv2c style="${o.snmp.version === 'v2c' ? '' : 'display:none'}">${f('Community', 'snmp.community', o.snmp.community)}</div>
      <div data-snmpv3 style="${o.snmp.version === 'v3' ? '' : 'display:none'}">
        ${f('User', 'snmp.user', o.snmp.user)}${f('Auth Password', 'snmp.auth_pass', o.snmp.auth_pass)}${f('Priv Password', 'snmp.priv_pass', o.snmp.priv_pass)}
      </div>
      ${f('Port', 'snmp.port', o.snmp.port)}`;
    const api = f('Endpoint', 'api.endpoint', o.api.endpoint, 'https://device/api') + f('Token', 'api.token', o.api.token);

    return common + `
      <div class="editor-sec">ADDRESS</div>
      ${f('IP address', 'ip', o.ip, '192.168.0.10')}
      <div class="editor-sec">COLLECTION METHODS</div>
      ${methodBlock('snmp', 'SNMP', o.snmp.on, snmp)}
      ${methodBlock('api', 'API', o.api.on, api)}
      <p class="editor-note">ICMP reachability is probed automatically. ICMP timing is configured globally under System.</p>`;
  }

  // Read the whole editor form into an object (type-aware; missing fields default).
  function collectForm(body) {
    const g = (id) => { const e = body.querySelector(`[data-f="${id}"]`); return e ? (e.type === 'checkbox' ? e.checked : e.value.trim()) : undefined; };
    const on = (id) => { const e = body.querySelector(`[data-mcheck="${id}"]`); return !!(e && e.checked); };
    const type = g('type') || 'on-prem';
    return {
      type,
      ip: g('ip') || '',
      provider: g('provider') || '',
      account: g('account') || '',
      description: g('description') || '',
      enabled: g('enabled') !== false,
      snmp: { on: on('snmp'), version: g('snmp.version') || 'v2c', community: g('snmp.community') || '',
              port: +g('snmp.port') || 161, user: g('snmp.user') || '',
              auth_pass: g('snmp.auth_pass') || '', priv_pass: g('snmp.priv_pass') || '' },
      api:  { on: type === 'cloud' ? true : on('api'), endpoint: g('api.endpoint') || '', token: g('api.token') || '' },
    };
  }

  function wireEditor() {
    const body = document.getElementById('soBody');

    // Type switch → rebuild the form for that type, preserving entered values.
    body.querySelector('[data-typesel]')?.addEventListener('change', (e) => {
      const cur = collectForm(body);
      cur.type = e.target.value;
      body.innerHTML = editorForm(normalize(cur));
      wireEditor();
    });

    body.querySelectorAll('[data-mcheck]').forEach(cb => cb.addEventListener('change', () => {
      body.querySelector(`[data-mblock="${cb.dataset.mcheck}"]`).classList.toggle('on', cb.checked);
    }));
    const verSel = body.querySelector('[data-snmpver]');
    if (verSel) verSel.addEventListener('change', () => {
      body.querySelector('[data-snmpv2c]').style.display = verSel.value === 'v2c' ? '' : 'none';
      body.querySelector('[data-snmpv3]').style.display  = verSel.value === 'v3'  ? '' : 'none';
    });

    document.getElementById('soCancel').onclick = closeEditor;
    document.getElementById('soClose').onclick = closeEditor;
    document.getElementById('soOverlay').onclick = closeEditor;

    const delBtn = document.getElementById('soDelete');
    if (delBtn) delBtn.onclick = () => { state.objects.splice(editIdx, 1); closeEditor(); render(); refreshPending(); };

    document.getElementById('soSave').onclick = () => {
      const no = collectForm(body);
      if (no.type === 'cloud') {
        if (!no.provider) { alert('Provider is required for a cloud object.'); return; }
        no.ip = ''; no.snmp.on = false; no.api.on = true;   // cloud identity = provider+account; API-monitored
      } else {
        if (!no.ip) { alert('IP address is required for an on-prem object.'); return; }
        no.provider = ''; no.account = '';
      }
      if (editIdx == null) state.objects.push(no); else state.objects[editIdx] = no;
      closeEditor(); render(); refreshPending();
    };
  }

  // ── Wiring (table) ────────────────────────────────────────────────────────────
  function wire() {
    const el = document.getElementById('contentBody');
    document.getElementById('cfgAdd')?.addEventListener('click', () => openEditor(null));
    el.querySelectorAll('[data-edit-btn]').forEach(b => b.addEventListener('click', () => openEditor(+b.dataset.editBtn)));
    el.querySelectorAll('[data-toggle]').forEach(cb => cb.addEventListener('change', () => {
      state.objects[+cb.dataset.toggle].enabled = cb.checked; render(); refreshPending();
    }));
    el.querySelectorAll('[data-del]').forEach(b => b.addEventListener('click', () => {
      state.objects.splice(+b.dataset.del, 1); render(); refreshPending();
    }));
  }

  // ── Commit: side-by-side JSON diff → publish → progress bar ──────────────────
  const RELOAD_TIMEOUT_MS = 30000;
  const POLL_MS = 800;

  function splitDiff(beforeStr, afterStr) {
    const a = beforeStr.split('\n'), b = afterStr.split('\n');
    const n = a.length, m = b.length;
    const dp = Array.from({ length: n + 1 }, () => new Int32Array(m + 1));
    for (let i = n - 1; i >= 0; i--)
      for (let j = m - 1; j >= 0; j--)
        dp[i][j] = a[i] === b[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
    const rows = [];
    let i = 0, j = 0, remq = [], addq = [];
    const flush = () => {
      for (let k = 0; k < Math.max(remq.length, addq.length); k++)
        rows.push({ l: remq[k] ?? null, r: addq[k] ?? null, ch: true });
      remq = []; addq = [];
    };
    while (i < n && j < m) {
      if (a[i] === b[j]) { flush(); rows.push({ l: a[i], r: b[j], ch: false }); i++; j++; }
      else if (dp[i + 1][j] >= dp[i][j + 1]) remq.push(a[i++]);
      else addq.push(b[j++]);
    }
    while (i < n) remq.push(a[i++]);
    while (j < m) addq.push(b[j++]);
    flush();
    return rows;
  }

  function diffHtml() {
    const before = { excluded_ips: deployed.excluded, probe_targets: deployed.objects };
    const after  = { excluded_ips: EXCLUDED_IPS.join(','), probe_targets: state.objects };
    const rows = splitDiff(JSON.stringify(before, null, 2), JSON.stringify(after, null, 2));
    const cell = (line, side, ch) => line == null
      ? `<div class="sd-line sd-empty"></div>`
      : `<div class="sd-line ${ch ? (side === 'l' ? 'sd-del' : 'sd-add') : ''}">${esc(line)}</div>`;
    return `
      <div class="cm-split">
        <div class="cm-split-pane"><div class="cm-split-head">Before</div>${rows.map(r => cell(r.l, 'l', r.ch)).join('')}</div>
        <div class="cm-split-pane"><div class="cm-split-head">After</div>${rows.map(r => cell(r.r, 'r', r.ch)).join('')}</div>
      </div>`;
  }

  function ensureModal() {
    let ov = document.getElementById('cmOverlay');
    if (ov) return ov;
    ov = document.createElement('div');
    ov.id = 'cmOverlay';
    ov.className = 'cm-overlay';
    ov.innerHTML = `
      <div class="cm-dialog" role="dialog" aria-modal="true">
        <div class="cm-head"><span class="cm-title" id="cmTitle">Review changes</span><button class="cm-close" id="cmClose" aria-label="Close">&times;</button></div>
        <div class="cm-body" id="cmBody"></div>
        <div class="cm-foot" id="cmFoot"></div>
      </div>`;
    document.body.appendChild(ov);
    ov.addEventListener('click', (e) => { if (e.target === ov) closeModal(); });
    ov.querySelector('#cmClose').addEventListener('click', closeModal);
    return ov;
  }
  function closeModal() { document.getElementById('cmOverlay')?.classList.remove('open'); }

  function openCommitModal() {
    refreshPending();
    if (JSON.stringify(state.objects) === JSON.stringify(deployed.objects)) return;
    const ov = ensureModal();
    ov.querySelector('#cmTitle').textContent = 'Review changes';
    ov.querySelector('#cmBody').innerHTML = diffHtml();
    ov.querySelector('#cmFoot').innerHTML = `
      <button class="btn-sm" id="cmCancel">Cancel</button>
      <button class="btn-primary btn-sm" id="cmPublish">Publish</button>`;
    ov.querySelector('#cmCancel').onclick = closeModal;
    ov.querySelector('#cmPublish').onclick = doCommit;
    ov.classList.add('open');
  }

  function progView() {
    return `
      <div class="cm-prog">
        <div class="cm-prog-head"><span id="cmProgLabel">Publishing</span><span id="cmProgPct">0%</span></div>
        <div class="cm-prog-track"><div class="cm-prog-fill" id="cmProgFill"></div></div>
      </div>`;
  }
  function setProg(pct, label, kind) {
    const f = document.getElementById('cmProgFill'), p = document.getElementById('cmProgPct'), l = document.getElementById('cmProgLabel');
    if (f) { f.style.width = Math.round(pct) + '%'; f.className = 'cm-prog-fill' + (kind ? ' ' + kind : ''); }
    if (p) p.textContent = Math.round(pct) + '%';
    if (l && label) l.textContent = label;
  }

  async function doCommit() {
    const staged = { excluded: EXCLUDED_IPS.join(','), objects: JSON.parse(JSON.stringify(state.objects)) };
    const body = document.getElementById('cmBody'), foot = document.getElementById('cmFoot');
    foot.innerHTML = `<button class="btn-sm" id="cmDone" disabled>Working…</button>`;
    body.innerHTML = progView();
    setProg(10, 'Publishing');

    try {
      const r = await fetch('/api/settings/commit', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ changes: commitPayload() }),
      });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const res = await r.json().catch(() => ({}));
      if ((res.applied | 0) <= 0) throw new Error('no changes applied (failed=' + (res.failed | 0) + ')');
    } catch (e) {
      setProg(100, 'Failed: ' + e.message, 'error');
      foot.innerHTML = `<button class="btn-sm" id="cmDone">Close</button>`;
      document.getElementById('cmDone').onclick = closeModal;
      return;
    }

    deployed = staged;
    window.NMS.setPendingChanges(0);
    pending = new Set(state.objects.filter(o => o.enabled && o.type !== 'cloud').map(o => o.ip));
    setProg(30, 'Applying');
    pollProgress(Date.now());
  }

  async function pollProgress(startTs) {
    if (!document.getElementById('cmProgFill')) return;
    let st;
    try {
      st = await fetch('/api/settings/reload-status', { credentials: 'same-origin', headers: { Accept: 'application/json' } })
        .then(r => r.json());
    } catch (_) { st = null; }

    const elapsed = Date.now() - startTs;
    if (st && st.status === 'complete') {
      setProg(100, 'Published', 'ok');
      pollStatus();
      const foot = document.getElementById('cmFoot');
      foot.innerHTML = `<button class="btn-primary btn-sm" id="cmDone">Done</button>`;
      document.getElementById('cmDone').onclick = closeModal;
      return;
    }
    if (elapsed >= RELOAD_TIMEOUT_MS) {
      setProg(100, 'Timed out — committed, still applying', 'warn');
      const foot = document.getElementById('cmFoot');
      foot.innerHTML = `<button class="btn-sm" id="cmDone">Close</button>`;
      document.getElementById('cmDone').onclick = closeModal;
      return;
    }
    const cur = parseInt(document.getElementById('cmProgFill').style.width) || 30;
    setProg(cur + (90 - cur) * 0.25, 'Applying');
    setTimeout(() => pollProgress(startTs), POLL_MS);
  }

  // ── Init ────────────────────────────────────────────────────────────────────
  window.NMS = window.NMS || {};
  window.NMS.onCommit(openCommitModal);
  window.NMS.onRefresh(async () => { await load(); render(); pollStatus(); });

  document.addEventListener('DOMContentLoaded', async () => {
    render();
    await load();
    render();
    if (TAB === 'inventory') startStatusPolling();
  });
})();
