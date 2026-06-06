/* dashboard.js — 데이터 폴링 & DOM 업데이트 */

(function () {
  'use strict';

  const POLL_INTERVAL = 5000; /* ms — 폴링 주기 */

  let pollTimer = null; /* setInterval 반환값 저장 → 나중에 clearInterval로 멈출 수 있음 */

  /* DOMContentLoaded 이후 한 번만 DOM 요소를 찾아 캐싱 */
  let refs = {};

  function resolveRefs() {
    refs = {
      mgmtStatus: document.getElementById('mgmtStatus'),
      mgmtSub:    document.getElementById('mgmtSub'),
      uptimeVal:  document.getElementById('uptimeVal'),
      promStatus: document.getElementById('promStatus'),
      nodeStatus: document.getElementById('nodeStatus'),
      aliveVal:   document.getElementById('aliveVal'),
      daemonList: document.getElementById('daemonList'),
      eventList:  document.getElementById('eventList'),
    };
  }

  /* ── Fetch ── */
  async function fetchJSON(url) {
    try {
      const r = await fetch(url, { credentials: 'same-origin' });

      /* 세션 만료 또는 미로그인 → 로그인 페이지로 이동 */
      if (r.status === 401) {
        window.location.href = '/index.html';
        return null;
      }

      if (!r.ok) throw new Error(r.status);
      return r.json();
    } catch {
      return null;
    }
  }

  /* ── 렌더 헬퍼 ── */

  function setStatus(el, text, cls) {
    if (!el) return;
    el.textContent = text;
    el.className   = 'card-value ' + (cls || '');
  }

  /* DOM API로 요소 생성 → innerHTML 미사용, XSS 안전 */
  function makeDaemonRow(daemon) {
    const statusLower = daemon.status.toLowerCase();
    const cls = statusLower === 'up'   ? 'up'
              : statusLower === 'wait' ? 'wait' : 'down';

    const row    = document.createElement('div');
    row.className = 'daemon-row';

    const name   = document.createElement('span');
    name.className   = 'daemon-name';
    name.textContent = daemon.name; /* textContent → XSS 없음 */

    const pill   = document.createElement('span');
    pill.className   = `status-pill ${cls}`;
    pill.textContent = daemon.status.toUpperCase();

    row.append(name, pill);
    return row;
  }

  function renderDaemons(daemons) {
    if (!refs.daemonList || !Array.isArray(daemons)) return;
    refs.daemonList.replaceChildren(...daemons.map(makeDaemonRow));
  }

  function makeEventRow(event) {
    const row = document.createElement('div');
    row.className = 'event-row';

    const src = document.createElement('div');
    src.className   = 'event-source';
    src.textContent = event.source; /* textContent → XSS 없음 */

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

  /* ── 폴링 ── */
  async function poll() {
    const data = await fetchJSON('/api/status');
    if (!data) return;

    setStatus(refs.mgmtStatus,
              data.management?.status || '--',
              data.management?.status === 'Active' ? 'green' : 'orange');

    if (refs.mgmtSub) refs.mgmtSub.textContent = data.management?.sub || '';

    if (refs.uptimeVal) {
      const s = data.uptime_seconds;
      refs.uptimeVal.textContent = (s != null)
        ? (window.NMS?.utils?.formatUptime(s) ?? s)
        : '--';
    }

    setStatus(refs.promStatus,
              data.prometheus?.status || '--',
              data.prometheus?.status === 'Connected' ? 'green' : 'orange');

    setStatus(refs.nodeStatus,
              data.node_exporter?.status || '--',
              data.node_exporter?.status === 'Connected' ? 'green' : 'orange');

    if (refs.aliveVal) {
      refs.aliveVal.textContent = data.alive_devices ?? '--';
    }

    renderDaemons(data.daemons);
    renderEvents(data.events);
  }

  /* ── 초기화 ── */
  document.addEventListener('DOMContentLoaded', () => {
    resolveRefs();

    document.getElementById('refreshBtn')
      ?.addEventListener('click', () => poll());

    document.getElementById('exportBtn')
      ?.addEventListener('click', () => {
        /* TODO: 실제 export 엔드포인트 연결 필요 */
        alert('Export triggered');
      });

    document.getElementById('filterBtn')
      ?.addEventListener('click', () => {
        const q = document.getElementById('metricSearch')?.value.trim();
        console.log('Filter metric:', q);
        /* TODO: Grafana iframe URL에 필터 파라미터 전달 */
      });

    poll(); /* 즉시 첫 폴링 */
    pollTimer = setInterval(poll, POLL_INTERVAL); /* 반환값 저장 → 필요 시 clearInterval(pollTimer) */
  });

}());
