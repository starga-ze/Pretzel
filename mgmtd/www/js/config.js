/* config.js — Configuration ▸ Inventory. Renders the managed-object table into #contentBody.
 *
 * Every object is modeled on two orthogonal axes:
 *   • type     — what it is:  firewall | switch | router | ddos | npb | lb | waf | sase | saas
 *   • platform — the access type, i.e. how the API is reached:
 *        direct — IP-based API platform, reached at its own address (mgmt IP/FQDN)  [on-prem/VM]
 *        tenant — cloud-based API platform, reached via a tenant-scoped API         [SASE/SaaS/SCM]
 * `target` is the access identifier (direct → IP/FQDN, tenant → tenant/TSG id).
 * Each object carries an immutable UUID (id). Collection/credentials are onboarded separately.
 *
 * Publish opens a review modal with a side-by-side JSON diff, then a progress bar while the
 * reload converges. Status: direct = ICMP reachability, tenant = API health (wired later).
 */
(function () {
  'use strict';

  const TAB = new URLSearchParams(location.search).get('tab') || 'inventory';
  const EXCLUDED_IPS = ['127.0.0.1'];
  const STATUS_POLL_MS = 5000;

  const TYPES = [
    ['firewall', 'Firewall'], ['switch', 'Switch'], ['router', 'Router'],
    ['ddos', 'DDoS'], ['npb', 'NPB'], ['lb', 'Load Balancer'], ['waf', 'WAF'],
    ['sase', 'SASE'], ['saas', 'SaaS'],
  ];
  const TYPE_LABEL = Object.fromEntries(TYPES);

  const state = { objects: [] };   // [{ id, type, platform, name, description, target, enabled }]
  let deployed = { excluded: '', objects: [] };
  let editIdx = null;
  let draftId = null;              // uuid of the object being edited (survives access/type rebuilds)
  let liveAlive = new Set();
  let statusReady = false;
  let pending = new Set();
  let statusTimer = null;

  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"]/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  function uuid() {
    if (crypto && crypto.randomUUID) return crypto.randomUUID();
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
      const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
    });
  }

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
    const platform = (t.platform || t.access) === 'tenant' ? 'tenant' : 'direct';   // `access` = legacy key
    return {
      id: t.id || uuid(),
      type: TYPE_LABEL[t.type] ? t.type : 'firewall',
      platform,
      name: t.name || '',
      description: t.description || '',
      target: t.target || '',
      enabled: t.enabled !== false,
    };
  }

  function blankObject() {
    return normalize({ platform: 'direct', type: 'firewall', enabled: true });
  }

  const accessLabel = (o) => (o.platform === 'tenant' ? 'Tenant ID' : 'IP/FQDN');

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

  // active · down(no response) · pending · off(disabled). direct=ICMP, tenant=API health.
  function statusOf(o) {
    if (!o.enabled) return 'off';
    if (o.platform === 'tenant') return 'pending';   // TODO: from tenant API health once wired
    if (pending.has(o.target) || !statusReady) return 'pending';
    return liveAlive.has(o.target) ? 'active' : 'down';
  }

  const STATUS_LABEL = { active: 'Active', down: 'No response', pending: 'Pending', off: 'Disabled' };
  const STATUS_TIP   = { active: 'Reachable', down: 'No response', pending: 'Awaiting first check', off: 'Disabled — not monitored' };

  function paintStatus() {
    document.querySelectorAll('[data-st-id]').forEach(dot => {
      const o = state.objects.find(x => x.id === dot.dataset.stId);
      const s = o ? statusOf(o) : 'off';
      dot.className = 'st-dot ' + s;
      dot.title = STATUS_LABEL[s] + ' — ' + STATUS_TIP[s];
    });
  }

  // ── Render ─────────────────────────────────────────────────────────────────────
  function render() {
    const el = document.getElementById('contentBody');
    if (!el) return;

    if (TAB !== 'inventory') { el.innerHTML = `<div class="cfg-empty">Nothing here yet.</div>`; return; }

    const rows = state.objects.length
      ? state.objects.map((o, i) => `
        <tr class="${o.enabled ? '' : 'row-off'}">
          <td class="col-name">
            <div class="name-line">
              <span class="st-dot ${statusOf(o)}" data-st-id="${esc(o.id)}" title="${STATUS_LABEL[statusOf(o)]}"></span>
              <span class="cell-name">${esc(o.name) || '<span class="muted">—</span>'}</span>
            </div>
            ${o.description ? `<div class="cell-sub">${esc(o.description)}</div>` : ''}
          </td>
          <td class="col-uuid"><span class="uuid-chip" title="${esc(o.id)}">${esc(o.id)}</span></td>
          <td class="col-type"><span class="type-badge">${esc(TYPE_LABEL[o.type] || o.type)}</span></td>
          <td class="col-access"><span class="access-badge ${o.platform}">${accessLabel(o)}</span></td>
          <td class="col-endpoint">${esc(o.target) || '<span class="muted">—</span>'}</td>
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

    const direct = state.objects.filter(o => o.platform !== 'tenant').length;
    const tenant = state.objects.length - direct;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">Inventory</span>
            <span class="cfg-h-sub">${direct} direct · ${tenant} tenant · excluded: ${EXCLUDED_IPS.join(', ')}</span>
          </div>
          <button class="btn-primary btn-sm" id="cfgAdd">+ Add Object</button>
        </div>

        <table class="cfg-table cfg-table-inv">
          <thead><tr>
            <th class="col-name">Name</th>
            <th class="col-uuid">UUID</th>
            <th class="col-type">Device Type</th>
            <th class="col-access">Access Type</th>
            <th class="col-endpoint">Access Identifier</th>
            <th class="col-en">Enable</th>
            <th class="col-act"></th>
          </tr></thead>
          <tbody>${rows}</tbody>
        </table>

        <div class="st-legend">
          <span><i class="st-dot active"></i>Active</span>
          <span><i class="st-dot down"></i>No response</span>
          <span><i class="st-dot pending"></i>Pending</span>
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
    draftId = o.id;
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

  function fieldRow(label, id, val, ph) {
    return `<div class="field-row"><label>${label}</label><input data-f="${id}" value="${esc(val)}" placeholder="${esc(ph || '')}"/></div>`;
  }

  function editorForm(o) {
    const f = fieldRow;
    const typeOpts = TYPES.map(([v, l]) => `<option value="${v}" ${o.type === v ? 'selected' : ''}>${l}</option>`).join('');
    const isTenant = o.platform === 'tenant';

    return `
      <div class="field-row"><label>Access Type</label>
        <select data-f="platform" data-platsel>
          <option value="direct" ${!isTenant ? 'selected' : ''}>IP/FQDN (IP-based API platform)</option>
          <option value="tenant" ${isTenant ? 'selected' : ''}>Tenant ID (cloud-based API platform)</option>
        </select></div>
      <div class="field-row"><label>Type</label>
        <select data-f="type">${typeOpts}</select></div>
      ${f('Name', 'name', o.name, 'e.g. core-fw-01')}
      ${f('Description', 'description', o.description, 'optional')}
      <label class="field-check"><input type="checkbox" data-f="enabled" ${o.enabled ? 'checked' : ''}/><span>Enabled</span></label>
      <div class="editor-sec">ACCESS IDENTIFIER</div>
      ${isTenant
        ? f('Tenant ID / TSG', 'target', o.target, 'e.g. 1963594622')
        : f('Management IP / FQDN', 'target', o.target, '192.168.0.10')}
      <p class="editor-note">${isTenant
        ? 'Identified by its tenant id (TSG); health-checked via the cloud platform API. Credentials are bound separately.'
        : 'Reached at its management address; reachability probed by ICMP.'}</p>`;
  }

  // Read the editor form into an object. `id` is carried out-of-band (draftId) so it survives
  // the access rebuild and stays immutable for the object's lifetime.
  function collectForm(body) {
    const g = (id) => { const e = body.querySelector(`[data-f="${id}"]`); return e ? (e.type === 'checkbox' ? e.checked : e.value.trim()) : undefined; };
    return {
      id: draftId,
      platform: g('platform') === 'tenant' ? 'tenant' : 'direct',
      type: g('type') || 'firewall',
      name: g('name') || '',
      description: g('description') || '',
      target: g('target') || '',
      enabled: g('enabled') !== false,
    };
  }

  function wireEditor() {
    const body = document.getElementById('soBody');

    // Platform switch → rebuild the target field/labels, preserving entered values + id.
    body.querySelector('[data-platsel]')?.addEventListener('change', () => {
      body.innerHTML = editorForm(normalize(collectForm(body)));
      wireEditor();
    });

    document.getElementById('soCancel').onclick = closeEditor;
    document.getElementById('soClose').onclick = closeEditor;
    document.getElementById('soOverlay').onclick = closeEditor;

    const delBtn = document.getElementById('soDelete');
    if (delBtn) delBtn.onclick = () => { state.objects.splice(editIdx, 1); closeEditor(); render(); refreshPending(); };

    document.getElementById('soSave').onclick = () => {
      const no = collectForm(body);
      if (!no.target) { alert(no.platform === 'tenant' ? 'Tenant ID is required.' : 'Management IP is required.'); return; }
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
    pending = new Set(state.objects.filter(o => o.enabled && o.platform === 'direct').map(o => o.target));
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
