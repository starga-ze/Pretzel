/* devices.js */
(function () {
  'use strict';

  let allDevices  = [];
  let activeFilter = 'all';
  let searchQuery  = '';

  function escHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function muted(text) {
    return text ? escHtml(text) : '<span class="muted">—</span>';
  }

  function statusDot(status) {
    const alive = status === 'alive';
    return `<span class="status-dot ${alive ? 'alive pulse' : 'dead'}"></span>`;
  }

  function uptimeStr(ticks) {
    if (!ticks) return null;
    const totalSec = Math.floor(ticks / 100);
    const d = Math.floor(totalSec / 86400);
    const h = Math.floor((totalSec % 86400) / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    if (d > 0) return `${d}d ${h}h`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m`;
  }

  function renderRow(d) {
    const alive    = d.status === 'alive';
    const hasSnmp  = !!(d.sys_descr || d.sys_contact || d.sys_object_id || d.sys_up_time_ticks);
    const rowId    = 'dev-' + d.ip.replace(/\./g, '-');

    const tr = document.createElement('tr');
    tr.className = 'device-row';
    tr.dataset.ip     = d.ip;
    tr.dataset.status = d.status;
    if (hasSnmp) tr.dataset.expandable = '1';

    tr.innerHTML = `
      <td class="col-status">${statusDot(d.status)}</td>
      <td class="col-ip"><code>${escHtml(d.ip)}</code></td>
      <td class="col-hostname">${muted(d.hostname)}</td>
      <td class="col-location">${muted(d.sys_location)}</td>
      <td class="col-status-text">
        <span class="status-badge ${alive ? 'alive' : 'dead'}">${alive ? 'Alive' : 'Dead'}</span>
      </td>
      <td class="col-latency">${d.latency_ms != null ? d.latency_ms + ' ms' : '<span class="muted">—</span>'}</td>
      <td class="col-expand">${hasSnmp ? '<button class="expand-btn" title="SNMP details">&#x25BC;</button>' : ''}</td>`;

    if (hasSnmp) {
      const detail = document.createElement('tr');
      detail.className = 'device-detail-row hidden';
      detail.id = rowId + '-detail';

      const uptime = uptimeStr(d.sys_up_time_ticks);
      detail.innerHTML = `
        <td colspan="7" class="device-detail-cell">
          <div class="device-detail-grid">
            ${d.sys_descr     ? `<div class="dg-item"><span class="dg-label">Description</span><span class="dg-value">${escHtml(d.sys_descr)}</span></div>` : ''}
            ${d.sys_contact   ? `<div class="dg-item"><span class="dg-label">Contact</span><span class="dg-value">${escHtml(d.sys_contact)}</span></div>` : ''}
            ${d.sys_object_id ? `<div class="dg-item"><span class="dg-label">OID</span><span class="dg-value"><code>${escHtml(d.sys_object_id)}</code></span></div>` : ''}
            ${uptime          ? `<div class="dg-item"><span class="dg-label">Uptime</span><span class="dg-value">${uptime}</span></div>` : ''}
          </div>
        </td>`;

      tr.querySelector('.expand-btn')?.addEventListener('click', e => {
        e.stopPropagation();
        const open = !detail.classList.contains('hidden');
        detail.classList.toggle('hidden', open);
        e.currentTarget.innerHTML = open ? '&#x25BC;' : '&#x25B2;';
      });

      return [tr, detail];
    }

    return [tr];
  }

  function applyFilters() {
    const tbody = document.getElementById('deviceTableBody');
    if (!tbody) return;

    const q = searchQuery.toLowerCase();
    const filtered = allDevices.filter(d => {
      if (activeFilter === 'alive' && d.status !== 'alive') return false;
      if (activeFilter === 'dead'  && d.status === 'alive') return false;
      if (q && !d.ip.includes(q) &&
          !(d.hostname     || '').toLowerCase().includes(q) &&
          !(d.sys_location || '').toLowerCase().includes(q)) return false;
      return true;
    });

    if (filtered.length === 0) {
      tbody.innerHTML = `<tr><td colspan="7" class="loading-row">No devices match the current filter.</td></tr>`;
      return;
    }

    const rows = filtered.flatMap(renderRow);
    tbody.replaceChildren(...rows);
  }

  async function load() {
    const tbody = document.getElementById('deviceTableBody');
    if (tbody) tbody.innerHTML = `<tr><td colspan="7" class="loading-row">Loading…</td></tr>`;

    try {
      const data = await window.NMS.utils.fetchJSON('/api/devices');
      if (!data) return;

      allDevices = data.devices || [];

      const badge = document.getElementById('devicesBadge');
      if (badge) badge.textContent = `${allDevices.length} device${allDevices.length !== 1 ? 's' : ''}`;

      applyFilters();
    } catch (e) {
      if (tbody) tbody.innerHTML = `<tr><td colspan="7" class="loading-row error">Failed to load devices: ${e}</td></tr>`;
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('refreshBtn')?.addEventListener('click', load);

    document.getElementById('deviceSearch')?.addEventListener('input', e => {
      searchQuery = e.target.value.trim();
      applyFilters();
    });

    document.querySelectorAll('.table-filter-chip').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.table-filter-chip').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        activeFilter = btn.dataset.filter;
        applyFilters();
      });
    });

    load();
    setInterval(load, 30000);
  });
}());
