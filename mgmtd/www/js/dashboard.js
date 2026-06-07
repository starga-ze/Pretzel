/* dashboard.js — heartbeat polling & DOM update */

(function () {
  'use strict';

  const POLL_INTERVAL = 5000;

  let pollTimer = null;
  let refs = {};

  const DAEMON_META = {
    ipcd:          { label: 'IPC',          icon: '🔌', raw: 'pz-ipcd' },
    engined:       { label: 'Engine',       icon: '⚙️', raw: 'pz-engined' },
    authd:         { label: 'Auth',         icon: '🔐', raw: 'pz-authd' },
    icmpd:         { label: 'ICMP',         icon: '📡', raw: 'pz-icmpd' },
    snmpd:         { label: 'SNMP',         icon: '📊', raw: 'pz-snmpd' },
    topologyd:     { label: 'Topology',     icon: '🗺️', raw: 'pz-topologyd' },
    mgmtd:         { label: 'Mgmt',         icon: '🖥️', raw: 'pz-mgmtd' },
    prometheus:    { label: 'Prometheus',   icon: '📈', raw: 'prometheus' },
    grafana:       { label: 'Grafana',      icon: '📉', raw: 'grafana' },
    node_exporter: { label: 'Node Exporter',icon: '🖧', raw: 'node_exporter' },
  };

  function resolveRefs() {
    refs = {
      uptimeVal:   document.getElementById('uptimeVal'),
      aliveVal:    document.getElementById('aliveVal'),
      daemonGrid:  document.getElementById('daemonGrid'),
      eventList:   document.getElementById('eventList'),
      hbTimestamp: document.getElementById('hbTimestamp'),
    };
  }

  async function fetchJSON(url) {
    try {
      const r = await fetch(url, { credentials: 'same-origin' });
      if (r.status === 401) { window.location.href = '/index.html'; return null; }
      if (!r.ok) throw new Error(r.status);
      return r.json();
    } catch {
      return null;
    }
  }

  /* ── Daemon card ── */
  function makeDaemonCard(daemon) {
    const alive = daemon.status === 'alive';
    const meta  = DAEMON_META[daemon.name] || { label: daemon.name, icon: '⚙️' };

    const card = document.createElement('div');
    card.className = 'daemon-card' + (alive ? ' alive' : ' dead');

    /* pulse dot */
    const dot = document.createElement('span');
    dot.className = 'daemon-dot' + (alive ? ' pulse' : '');

    /* icon */
    const icon = document.createElement('span');
    icon.className   = 'daemon-card-icon';
    icon.textContent = meta.icon;

    /* name */
    const name = document.createElement('div');
    name.className   = 'daemon-card-name';
    name.textContent = meta.label;

    /* raw name sub-label */
    const raw = document.createElement('div');
    raw.className   = 'daemon-card-raw';
    raw.textContent = meta.raw || daemon.name;

    /* status badge */
    const badge = document.createElement('div');
    badge.className   = 'daemon-card-badge ' + (alive ? 'alive' : 'dead');
    badge.textContent = alive ? 'ALIVE' : 'DEAD';

    /* latency */
    const latency = document.createElement('div');
    latency.className   = 'daemon-card-latency';
    latency.textContent = (alive && daemon.latency_ms != null)
      ? daemon.latency_ms + ' ms'
      : '—';

    const top  = document.createElement('div');
    top.className = 'daemon-card-top';
    top.append(dot, icon, name);

    card.append(top, raw, badge, latency);
    return card;
  }

  function renderDaemons(daemons) {
    if (!refs.daemonGrid) return;

    if (!Array.isArray(daemons) || daemons.length === 0) {
      refs.daemonGrid.replaceChildren(
        Object.assign(document.createElement('div'), {
          className: 'loading-row',
          textContent: 'Waiting for heartbeat data…'
        })
      );
      return;
    }

    refs.daemonGrid.replaceChildren(...daemons.map(makeDaemonCard));
  }

  function makeEventRow(event) {
    const row = document.createElement('div');
    row.className = 'event-row';

    const src = document.createElement('div');
    src.className   = 'event-source';
    src.textContent = event.source;

    const msg = document.createElement('div');
    msg.className   = 'event-msg';
    msg.textContent = event.message;

    row.append(src, msg);
    return row;
  }

  function renderEvents(events) {
    if (!refs.eventList || !Array.isArray(events)) return;
    refs.eventList.replaceChildren(...events.slice(0, 8).map(makeEventRow));
  }

  function formatTimestamp(ms) {
    if (!ms) return '';
    const d = new Date(ms);
    return 'Last poll: ' + d.toLocaleTimeString();
  }

  /* ── Poll ── */
  async function poll() {
    const data = await fetchJSON('/api/status');
    if (!data) return;

    if (refs.uptimeVal) {
      const s = data.uptime_seconds;
      refs.uptimeVal.textContent = (s != null)
        ? (window.NMS?.utils?.formatUptime(s) ?? s)
        : '--';
    }

    if (refs.aliveVal) {
      refs.aliveVal.textContent = data.alive_devices ?? '--';
    }

    if (refs.hbTimestamp) {
      refs.hbTimestamp.textContent = formatTimestamp(data.timestamp_ms);
    }

    renderDaemons(data.daemons);
    renderEvents(data.events);
  }

  /* ── Init ── */
  document.addEventListener('DOMContentLoaded', () => {
    resolveRefs();

    document.getElementById('refreshBtn')
      ?.addEventListener('click', () => poll());

    document.getElementById('exportBtn')
      ?.addEventListener('click', () => alert('Export triggered'));

    poll();
    pollTimer = setInterval(poll, POLL_INTERVAL);
  });

}());
