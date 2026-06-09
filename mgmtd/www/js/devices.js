/* devices.js */
(function () {
  'use strict';

  let allDevices = [];
  let activeFilter = 'all';
  let searchQuery  = '';

  function statusDot(status) {
    const alive = status === 'alive';
    return `<span class="status-dot ${alive ? 'alive pulse' : 'dead'}"></span>`;
  }

  function renderRow(d) {
    const alive = d.status === 'alive';
    const tr = document.createElement('tr');
    tr.dataset.ip     = d.ip;
    tr.dataset.status = d.status;
    tr.innerHTML = `
      <td class="col-status">${statusDot(d.status)}</td>
      <td class="col-ip"><code>${d.ip}</code></td>
      <td class="col-hostname">${d.hostname || '<span class="muted">—</span>'}</td>
      <td class="col-device">${d.device_name || '<span class="muted">—</span>'}</td>
      <td class="col-vendor">${d.vendor || '<span class="muted">—</span>'}</td>
      <td class="col-status-text">
        <span class="status-badge ${alive ? 'alive' : 'dead'}">${alive ? 'Alive' : 'Dead'}</span>
      </td>
      <td class="col-latency">${d.latency_ms != null ? d.latency_ms + ' ms' : '<span class="muted">—</span>'}</td>`;
    return tr;
  }

  function applyFilters() {
    const tbody = document.getElementById('deviceTableBody');
    if (!tbody) return;

    const q = searchQuery.toLowerCase();
    const filtered = allDevices.filter(d => {
      if (activeFilter === 'alive' && d.status !== 'alive') return false;
      if (activeFilter === 'dead'  && d.status === 'alive') return false;
      if (q && !d.ip.includes(q) &&
          !(d.hostname || '').toLowerCase().includes(q) &&
          !(d.device_name || '').toLowerCase().includes(q)) return false;
      return true;
    });

    if (filtered.length === 0) {
      tbody.innerHTML = `<tr><td colspan="7" class="loading-row">검색 결과가 없습니다.</td></tr>`;
      return;
    }

    tbody.replaceChildren(...filtered.map(renderRow));
  }

  async function load() {
    const tbody = document.getElementById('deviceTableBody');
    if (tbody) tbody.innerHTML = `<tr><td colspan="7" class="loading-row">로드 중…</td></tr>`;

    try {
      const data = await window.NMS.utils.fetchJSON('/api/devices');
      if (!data) return;

      allDevices = data.devices || [];

      const badge = document.getElementById('devicesBadge');
      if (badge) badge.textContent = `${allDevices.length} devices`;

      applyFilters();
    } catch (e) {
      if (tbody) tbody.innerHTML = `<tr><td colspan="7" class="loading-row error">로드 실패: ${e}</td></tr>`;
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
