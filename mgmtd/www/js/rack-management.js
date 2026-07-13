/* rack-management.js — Resource ▸ Rack View (READ-ONLY visualization).
 *
 * Shows every rack (from running-config engined.service.rack.racks) as its full vertical
 * U list, with assigned devices coloured by probe match (green = found in probe inventory,
 * red = not). All editing — racks and device allocation — lives in Configuration ▸ Rack
 * List; this page never writes. */
(function () {
  'use strict';

  const u = window.NMS.utils;

  let racks = [];
  let liveIps = new Set();

  function escHtml(s) {
    return String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  function isOnline(ip) { return liveIps.has(ip); }
  function rackHeight(r) { return parseInt(r.type, 10) || 42; }

  async function load() {
    try {
      const [settings, devs] = await Promise.all([
        u.fetchJSON('/api/settings'),
        u.fetchJSON('/api/devices'),
      ]);
      if (!settings || !devs) return;
      racks = (((settings.daemons || {}).engined || {}).rack || {}).racks || [];
      liveIps = new Set();
      (devs.devices || []).forEach(d => (d.ips || [d.primary_ip]).forEach(ip => liveIps.add(ip)));
    } catch (e) {
      document.getElementById('rackMgmt').innerHTML =
        `<div class="section-card"><div class="loading-row">Failed to load (${escHtml(e.message)})</div></div>`;
      return;
    }
    render();
  }

  function render() {
    const host = document.getElementById('rackMgmt');
    if (!racks.length) {
      host.innerHTML = `<div class="section-card"><div style="padding:24px;color:var(--text-muted);font-size:13px">
        No racks defined. Create and allocate devices in <strong>Configuration ▸ Rack List</strong>.</div></div>`;
      return;
    }

    host.innerHTML = `<div class="rack-view-grid">` + racks.map(r => {
      const height = rackHeight(r);
      // Map every covered unit to its device; emit ONE merged block per device (sized to
      // span its range) at the top unit, skipping the units below it.
      const occ = {};
      (r.devices || []).forEach(d => {
        const size = d.size || 1;
        for (let i = 0; i < size; i++) occ[d.unit + i] = { d, top: i === size - 1 };
      });

      let rows = '';
      for (let n = height; n >= 1; n--) {
        const cell = occ[n];
        if (cell && !cell.top) continue;     // covered by a block already emitted above
        if (cell) {
          const d = cell.d, online = isOnline(d.ip), size = d.size || 1;
          const label = size > 1 ? `U${d.unit}–U${d.unit + size - 1}` : `U${n}`;
          const multi = size > 1 ? ' rk-u-multi' : '';
          const span = size > 1
            ? ` style="height:calc(var(--rk-uh) * ${size} + var(--rk-gap) * ${size - 1})"` : '';
          rows += `<div class="rk-u rk-u-filled${multi} ${online ? '' : 'rk-dev-disabled'}"${span}>
            <span class="rk-u-num">${label}</span>
            <span class="status-dot ${online ? 'alive' : 'dead'}"></span>
            <span class="rk-dev-name">${escHtml(d.device || d.ip)}</span>
            <code class="rk-dev-ip">${escHtml(d.ip)}</code>
            <span class="rk-stat ${online ? 'rk-stat-en' : 'rk-stat-dis'}">${online ? 'enable' : 'disable'}</span>
          </div>`;
        } else {
          rows += `<div class="rk-u rk-u-empty">
            <span class="rk-u-num">U${n}</span>
            <span class="rk-dev-name muted">—</span>
          </div>`;
        }
      }

      const used = (r.devices || []).length;
      return `<div class="section-card rack-view-card">
        <div class="section-header">
          <span class="section-title">${escHtml(r.name)}${r.description ? ` <span class="muted">${escHtml(r.description)}</span>` : ''}</span>
          <span class="devices-count-badge">${used}/${height}U</span>
        </div>
        <div class="rk-u-list">${rows}</div>
      </div>`;
    }).join('') + `</div>`;
  }

  function init() {
    document.getElementById('refreshBtn')?.addEventListener('click', load);
    load();
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', init);
  else
    init();
}());
