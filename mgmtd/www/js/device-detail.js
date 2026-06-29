/* device-detail.js — single device detail view */
(function () {
  'use strict';

  const SUBTYPE_LABEL = {
    router: 'Router', switch: 'Switch', firewall: 'Firewall', ap: 'Access Point',
    gateway: 'Gateway', windows: 'Windows', linux: 'Linux/Unix', printer: 'Printer',
    hypervisor: 'Hypervisor', bmc: 'BMC/iDRAC', server: 'Server', unknown: '',
  };

  function escHtml(s) {
    return String(s ?? '')
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function uptimeStr(ticks) {
    if (!ticks) return null;
    const totalSec = Math.floor(ticks / 100);
    const d = Math.floor(totalSec / 86400);
    const h = Math.floor((totalSec % 86400) / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    const s = totalSec % 60;
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m ${s}s`;
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

  function item(label, value, mono) {
    if (value == null || value === '') return '';
    const cls = mono ? 'dg-value mono' : 'dg-value';
    return `<div class="dg-item"><span class="dg-label">${escHtml(label)}</span><span class="${cls}">${escHtml(value)}</span></div>`;
  }

  function render(d) {
    document.getElementById('detailTitle').textContent = d.hostname || d.primary_ip;
    document.getElementById('detailSubtitle').innerHTML =
      `<span class="type-badge ${badgeCls(d)}">${escHtml(typeLabel(d))}</span>
       ${d.vendor ? `<span class="vendor-chip">${escHtml(d.vendor)}</span>` : ''}
       <span class="status-badge alive" style="margin-left:8px">Alive</span>`;

    const uptime = uptimeStr(d.sys_up_time_ticks);

    const ipList = d.ips.map(ip => `<code>${escHtml(ip)}</code>`).join(' ');
    const macList = (d.interface_macs && d.interface_macs.length)
      ? d.interface_macs.map(m => `<code>${escHtml(m)}</code>`).join(' ')
      : '<span class="muted">—</span>';

    const ifaces = d.interfaces || [];
    const ifaceBlock = ifaces.length ? `
      <div class="section-card">
        <div class="section-header"><span class="section-title">Interface Addresses</span>
          <span class="devices-count-badge">${ifaces.length}</span></div>
        <table class="device-table">
          <thead><tr>
            <th class="col-ip">IP Address</th><th>Netmask</th><th>Interface</th><th>ifIndex</th>
          </tr></thead>
          <tbody>
            ${ifaces.map(i => `<tr>
              <td><code>${escHtml(i.ip)}</code></td>
              <td>${i.netmask ? `<code>${escHtml(i.netmask)}</code>` : '<span class="muted">—</span>'}</td>
              <td>${i.if_name ? escHtml(i.if_name) : '<span class="muted">—</span>'}</td>
              <td>${i.if_index || '<span class="muted">—</span>'}</td>
            </tr>`).join('')}
          </tbody>
        </table>
      </div>` : '';

    // IF-MIB ifTable inventory
    const IFTYPE = { 6: 'ethernet', 24: 'loopback', 53: 'virtual', 131: 'tunnel',
      135: 'l2vlan', 136: 'l3vlan', 161: 'lag', 1: 'other' };
    const operLabel = s => (s === 1 ? 'up' : s === 2 ? 'down' : s === 3 ? 'testing' : '—');
    const speedLabel = m => (!m ? '—' : m >= 1000 ? (m / 1000) + ' Gbps' : m + ' Mbps');

    const ift = d.if_table || [];
    const ifTableBlock = ift.length ? `
      <div class="section-card">
        <div class="section-header"><span class="section-title">Interfaces (ifTable)</span>
          <span class="devices-count-badge">${ift.length}</span></div>
        <table class="device-table">
          <thead><tr>
            <th>Interface</th><th>Alias</th><th>Type</th><th>Speed</th><th>Status</th><th>MAC</th>
          </tr></thead>
          <tbody>
            ${ift.map(i => `<tr>
              <td>${i.name ? escHtml(i.name) : '<span class="muted">—</span>'}</td>
              <td>${i.alias ? escHtml(i.alias) : '<span class="muted">—</span>'}</td>
              <td>${IFTYPE[i.type] || (i.type || '—')}</td>
              <td>${speedLabel(i.speed_mbps)}</td>
              <td><span class="status-badge ${i.oper_status === 1 ? 'alive' : 'dead'}">${operLabel(i.oper_status)}</span></td>
              <td>${i.mac ? `<code>${escHtml(i.mac)}</code>` : '<span class="muted">—</span>'}</td>
            </tr>`).join('')}
          </tbody>
        </table>
      </div>` : '';

    // LLDP neighbors (topology edges)
    const nbrs = d.lldp_neighbors || [];
    const lldpBlock = nbrs.length ? `
      <div class="section-card">
        <div class="section-header"><span class="section-title">LLDP Neighbors</span>
          <span class="devices-count-badge">${nbrs.length}</span></div>
        <table class="device-table">
          <thead><tr>
            <th>Local Port</th><th>Neighbor</th><th>Remote Port</th><th>Chassis</th>
          </tr></thead>
          <tbody>
            ${nbrs.map(n => `<tr>
              <td>${n.local_port_name ? escHtml(n.local_port_name) : (n.local_port || '<span class="muted">—</span>')}</td>
              <td>${n.remote_sys_name ? escHtml(n.remote_sys_name) : '<span class="muted">—</span>'}</td>
              <td>${n.remote_port_id ? `<code>${escHtml(n.remote_port_id)}</code>` : '<span class="muted">—</span>'}</td>
              <td>${n.remote_chassis_id ? `<code>${escHtml(n.remote_chassis_id)}</code>` : '<span class="muted">—</span>'}</td>
            </tr>`).join('')}
          </tbody>
        </table>
      </div>` : '';

    const snmpBlock = d.has_snmp ? `
      <div class="section-card">
        <div class="section-header"><span class="section-title">SNMP Attributes</span></div>
        <div class="device-detail-grid" style="padding:16px">
          ${item('System Name', d.hostname)}
          ${item('Description', d.sys_descr)}
          ${item('Object ID', d.sys_object_id, true)}
          ${item('Contact', d.sys_contact)}
          ${uptime ? item('Uptime', uptime) : ''}
        </div>
      </div>` : `
      <div class="section-card">
        <div class="section-header"><span class="section-title">SNMP Attributes</span></div>
        <div style="padding:16px;color:var(--text-muted);font-size:13px">
          No SNMP data — this device responded to ICMP only (likely a host/endpoint).
        </div>
      </div>`;

    document.getElementById('detailContent').innerHTML = `
      <div class="cards-grid cards-grid-4">
        <div class="card"><div class="card-label">Type</div><div class="card-value">${escHtml(typeLabel(d))}</div></div>
        <div class="card"><div class="card-label">Vendor</div><div class="card-value" style="font-size:15px">${d.vendor ? escHtml(d.vendor) : '—'}</div></div>
        <div class="card"><div class="card-label">IP Addresses</div><div class="card-value">${d.ips.length}</div></div>
        <div class="card"><div class="card-label">Interface MACs</div><div class="card-value muted">${d.interface_macs.length}</div></div>
      </div>

      <div class="section-card">
        <div class="section-header"><span class="section-title">Identity</span></div>
        <div class="device-detail-grid" style="padding:16px">
          ${d.vendor ? `<div class="dg-item"><span class="dg-label">Vendor</span><span class="dg-value">${escHtml(d.vendor)}</span></div>` : ''}
          ${d.host_mac ? `<div class="dg-item"><span class="dg-label">Host MAC</span><span class="dg-value"><code>${escHtml(d.host_mac)}</code></span></div>` : ''}
          <div class="dg-item"><span class="dg-label">Rack Location</span><span class="dg-value">${d.location ? escHtml(d.location) : '<span class="muted">unassigned</span>'}</span></div>
          <div class="dg-item"><span class="dg-label">IP Addresses</span><span class="dg-value">${ipList}</span></div>
          <div class="dg-item dg-wide"><span class="dg-label">Interface MACs</span><span class="dg-value">${macList}</span></div>
        </div>
      </div>

      ${ifaceBlock}
      ${ifTableBlock}
      ${lldpBlock}
      ${snmpBlock}`;
  }

  function notFound(ip) {
    document.getElementById('detailTitle').textContent = ip || 'Device';
    document.getElementById('detailSubtitle').textContent = 'Not found';
    document.getElementById('detailContent').innerHTML =
      `<div class="section-card"><div style="padding:20px;color:var(--text-muted)">
        Device <code>${escHtml(ip)}</code> was not found in the current inventory.
      </div></div>`;
  }

  async function load() {
    const ip = new URLSearchParams(location.search).get('ip') || '';
    if (!ip) { notFound(''); return; }

    try {
      const data = await window.NMS.utils.fetchJSON('/api/devices');
      if (!data) return;
      const list = data.devices || [];
      const dev = list.find(d => d.primary_ip === ip || (d.ips || []).includes(ip));
      if (dev) render(dev); else notFound(ip);
    } catch (e) {
      document.getElementById('detailSubtitle').textContent = 'Failed to load: ' + (e.message || e);
    }
  }

  document.addEventListener('DOMContentLoaded', load);
}());
