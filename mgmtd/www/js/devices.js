/* devices.js — grouped/classified device inventory (Network / Server / Unknown) */
(function () {
  'use strict';

  let allDevices  = [];
  let activeFilter = 'all';
  let searchQuery  = '';

  const SUBTYPE_LABEL = {
    router: 'Router', switch: 'Switch', firewall: 'Firewall', ap: 'Access Point',
    gateway: 'Gateway', hypervisor: 'Hypervisor', bmc: 'BMC/iDRAC', server: 'Server',
    windows: 'Windows', linux: 'Linux/Unix', printer: 'Printer', unknown: '',
  };

  function escHtml(s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  function muted(text) { return text ? escHtml(text) : '<span class="muted">—</span>'; }

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

  function typeLabel(d) {
    const sub = SUBTYPE_LABEL[d.subtype] || '';
    if (d.type === 'network') return sub || 'Network';
    if (d.type === 'server')  return sub || 'Server';
    return sub || 'Unknown';
  }
  function badgeCls(d) {
    return d.type === 'network' ? 'badge-network'
         : d.type === 'server'  ? 'badge-server' : 'badge-unknown';
  }
  function typeBadge(d) {
    return `<span class="type-badge ${badgeCls(d)}">${escHtml(typeLabel(d))}</span>`;
  }

  function ipCell(d) {
    const extra = d.ips.length > 1
      ? ` <span class="ip-count-badge" title="${escHtml(d.ips.join(', '))}">+${d.ips.length - 1}</span>`
      : '';
    return `<code>${escHtml(d.primary_ip)}</code>${extra}`;
  }

  function hostnameCell(d) {
    const name = d.hostname ? escHtml(d.hostname) : (d.vendor ? '' : '<span class="muted">—</span>');
    const vend = d.vendor ? `<div class="cell-sub">${escHtml(d.vendor)}</div>` : '';
    return `${name}${vend}`;
  }

  function renderRow(d) {
    const tr = document.createElement('tr');
    tr.className = 'device-row';
    tr.dataset.ip = d.primary_ip;
    tr.innerHTML = `
      <td class="col-status"><span class="status-dot alive pulse"></span></td>
      <td class="col-ip">${ipCell(d)}</td>
      <td class="col-hostname">${hostnameCell(d)}</td>
      <td class="col-type">${typeBadge(d)}</td>
      <td class="col-location">${muted(d.location)}</td>
      <td class="col-actions">
        <a class="btn-sm details-btn" href="device-detail?ip=${encodeURIComponent(d.primary_ip)}">Details</a>
      </td>`;

    tr.addEventListener('mouseenter', e => showCard(d, e));
    tr.addEventListener('mousemove', moveCard);
    tr.addEventListener('mouseleave', hideCard);
    return tr;
  }

  // ── Hover device card (follows the cursor) ──────────────────────────────────
  function cardHtml(d) {
    const uptime = uptimeStr(d.sys_up_time_ticks);
    const ifaceLines = (d.interfaces || []).slice(0, 4).map(i =>
      `<div class="dc-iface"><code>${escHtml(i.ip)}</code>${i.if_name ? ` <span class="muted">${escHtml(i.if_name)}</span>` : ''}</div>`
    ).join('');
    const moreIf = (d.interfaces || []).length > 4 ? `<div class="muted">+${d.interfaces.length - 4} more…</div>` : '';

    return `
      <div class="dc-head">
        <span class="dc-name">${escHtml(d.hostname || d.primary_ip)}</span>
        ${typeBadge(d)}
      </div>
      <div class="dc-rows">
        ${d.vendor ? `<div class="dc-row"><span class="dc-k">Vendor</span><span class="dc-v">${escHtml(d.vendor)}</span></div>` : ''}
        <div class="dc-row"><span class="dc-k">IPs</span><span class="dc-v">${d.ips.map(escHtml).join(', ')}</span></div>
        <div class="dc-row"><span class="dc-k">MACs</span><span class="dc-v">${d.interface_macs.length}</span></div>
        ${(d.lldp_neighbors || []).length ? `<div class="dc-row"><span class="dc-k">LLDP</span><span class="dc-v">${d.lldp_neighbors.length} neighbor(s)</span></div>` : ''}
        ${d.sys_descr ? `<div class="dc-row"><span class="dc-k">Desc</span><span class="dc-v dc-clamp">${escHtml(d.sys_descr)}</span></div>` : ''}
        ${uptime ? `<div class="dc-row"><span class="dc-k">Uptime</span><span class="dc-v">${uptime}</span></div>` : ''}
        ${ifaceLines ? `<div class="dc-row"><span class="dc-k">Ifaces</span><span class="dc-v">${ifaceLines}${moreIf}</span></div>` : ''}
        ${!d.has_snmp ? `<div class="dc-row"><span class="dc-v muted">No SNMP — ICMP only</span></div>` : ''}
      </div>`;
  }

  function positionCard(card, x, y) {
    const pad = 14;
    const w = card.offsetWidth || 300;
    const h = card.offsetHeight || 120;
    let left = x + pad;
    let top  = y + pad;
    if (left + w > window.innerWidth - 8)  left = x - w - pad;
    if (top + h > window.innerHeight - 8)  top = window.innerHeight - h - 8;
    if (top < 8) top = 8;
    card.style.left = Math.max(8, left) + 'px';
    card.style.top  = top + 'px';
  }

  function showCard(d, e) {
    const card = document.getElementById('deviceCard');
    if (!card) return;
    card.innerHTML = cardHtml(d);
    card.classList.add('visible');
    card.setAttribute('aria-hidden', 'false');
    positionCard(card, e.clientX, e.clientY);
  }
  function moveCard(e) {
    const card = document.getElementById('deviceCard');
    if (card && card.classList.contains('visible')) positionCard(card, e.clientX, e.clientY);
  }
  function hideCard() {
    const card = document.getElementById('deviceCard');
    if (!card) return;
    card.classList.remove('visible');
    card.setAttribute('aria-hidden', 'true');
  }

  // ── Rendering ───────────────────────────────────────────────────────────────
  function matches(d) {
    if (activeFilter !== 'all' && d.type !== activeFilter) return false;
    const q = searchQuery.toLowerCase();
    if (!q) return true;
    return d.primary_ip.includes(q)
      || d.ips.some(ip => ip.includes(q))
      || (d.hostname || '').toLowerCase().includes(q)
      || (d.location || '').toLowerCase().includes(q);
  }

  function fillSection(type, bodyId, badgeId, sectionId, list) {
    const tbody = document.getElementById(bodyId);
    const badge = document.getElementById(badgeId);
    const section = document.getElementById(sectionId);
    if (!tbody) return;

    if (badge) badge.textContent = `${list.length}`;
    if (section)
      section.style.display = (activeFilter !== 'all' && activeFilter !== type) ? 'none' : '';

    if (list.length === 0) {
      tbody.innerHTML = `<tr><td colspan="6" class="loading-row">No devices.</td></tr>`;
      return;
    }
    tbody.replaceChildren(...list.map(renderRow));
  }

  function applyFilters() {
    const filtered = allDevices.filter(matches);
    fillSection('network', 'networkBody', 'networkBadge', 'networkSection',
      filtered.filter(d => d.type === 'network'));
    fillSection('server', 'serverBody', 'serverBadge', 'serverSection',
      filtered.filter(d => d.type === 'server'));
    fillSection('unknown', 'unknownBody', 'unknownBadge', 'unknownSection',
      filtered.filter(d => d.type === 'unknown'));
  }

  async function load() {
    try {
      const data = await window.NMS.utils.fetchJSON('/api/devices');
      if (!data) return;

      allDevices = data.devices || [];
      const s = data.summary || { total: 0, network: 0, server: 0, unknown: 0 };
      document.getElementById('sumTotal').textContent   = s.total;
      document.getElementById('sumNetwork').textContent = s.network;
      document.getElementById('sumServer').textContent  = s.server;
      document.getElementById('sumUnknown').textContent = s.unknown;

      applyFilters();
    } catch (e) {
      ['networkBody', 'serverBody', 'unknownBody'].forEach(id => {
        const tb = document.getElementById(id);
        if (tb) tb.innerHTML = `<tr><td colspan="6" class="loading-row error">Failed to load: ${escHtml(e.message || e)}</td></tr>`;
      });
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
