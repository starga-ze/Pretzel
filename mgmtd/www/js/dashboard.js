/* dashboard.js */
(function () {
  'use strict';

  const POLL_MS = 10000;

  const DAEMON_META = {
    ipcd:          { label: 'IPC Broker',    raw: 'pz-ipcd' },
    engined:       { label: 'Engine',        raw: 'pz-engined' },
    authd:         { label: 'Auth',          raw: 'pz-authd' },
    icmpd:         { label: 'ICMP Probe',    raw: 'pz-icmpd' },
    snmpd:         { label: 'SNMP',          raw: 'pz-snmpd' },
    topologyd:     { label: 'Topology',      raw: 'pz-topologyd' },
    mgmtd:         { label: 'Management',    raw: 'pz-mgmtd' },
    prometheus:    { label: 'Prometheus',    raw: 'prometheus' },
    grafana:       { label: 'Grafana',       raw: 'grafana' },
    node_exporter: { label: 'Node Exporter', raw: 'node_exporter' },
  };

  function initial(name) {
    const m = DAEMON_META[name];
    return (m ? m.label : name).slice(0, 2).toUpperCase();
  }

  function makeDaemonCard(d) {
    const alive = d.status === 'alive';
    const meta  = DAEMON_META[d.name] || { label: d.name, raw: d.name };
    const card  = document.createElement('div');
    card.className = 'daemon-card ' + (alive ? 'alive' : 'dead');
    card.innerHTML = `
      <div class="daemon-card-top">
        <span class="daemon-dot${alive ? ' pulse' : ''}"></span>
        <div class="daemon-card-icon">${initial(d.name)}</div>
        <div class="daemon-card-name">${meta.label}</div>
      </div>
      <div class="daemon-card-raw">${meta.raw}</div>
      <div class="daemon-card-badge ${alive ? 'alive' : 'dead'}">${alive ? 'ALIVE' : 'DEAD'}</div>
      <div class="daemon-card-latency">${alive && d.latency_ms != null ? d.latency_ms + ' ms' : '—'}</div>`;
    return card;
  }

  function setBar(barId, valId, pct) {
    const bar = document.getElementById(barId);
    const val = document.getElementById(valId);
    if (bar) {
      bar.style.width = Math.min(pct, 100) + '%';
      bar.style.background = pct > 85 ? 'var(--red)' : pct > 65 ? '#f59e0b' : 'var(--accent)';
    }
    if (val) val.textContent = pct.toFixed(1) + '%';
  }

  async function pollMetrics() {
    try {
      const d = await window.NMS.utils.fetchJSON('/api/node-metrics');
      if (!d) return;
      setBar('cpuBar', 'cpuVal', d.cpu_pct ?? 0);
      setBar('memBar', 'memVal', d.mem_pct ?? 0);
      setBar('diskBar', 'diskVal', d.disk_pct ?? 0);
    } catch { /* ignore */ }
  }

  async function pollStatus() {
    try {
      const d = await window.NMS.utils.fetchJSON('/api/status');
      if (!d) return;

      const hbTs = document.getElementById('hbTimestamp');
      if (hbTs && d.timestamp_ms)
        hbTs.textContent = 'Last poll: ' + new Date(d.timestamp_ms).toLocaleTimeString();

      const grid = document.getElementById('daemonGrid');
      if (grid && Array.isArray(d.daemons) && d.daemons.length)
        grid.replaceChildren(...d.daemons.map(makeDaemonCard));
    } catch { /* ignore */ }
  }

  async function poll() {
    await Promise.all([pollMetrics(), pollStatus()]);
  }

  document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('refreshBtn')?.addEventListener('click', poll);
    poll();
    setInterval(poll, POLL_MS);
  });
}());