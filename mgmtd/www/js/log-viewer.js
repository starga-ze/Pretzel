/* log-viewer.js  –  table-based log viewer */
(function () {
  'use strict';

  // ── state ──────────────────────────────────────────────────
  let allRows     = [];   // parsed {time, daemon, level, msg, raw}
  let activeLevel = '';
  let searchKw    = '';
  let autoRefresh = true;
  let refreshTimer = null;
  const REFRESH_MS = 5000;

  // ── level detection ────────────────────────────────────────
  const LEVEL_ORDER = ['CRITICAL','CRIT','ERROR','WARN','WARNING','INFO','DEBUG','TRACE'];

  const LEVEL_META = {
    'CRITICAL': { cls: 'lv-badge-crit',  label: 'CRIT'  },
    'CRIT':     { cls: 'lv-badge-crit',  label: 'CRIT'  },
    'ERROR':    { cls: 'lv-badge-error', label: 'ERROR' },
    'WARN':     { cls: 'lv-badge-warn',  label: 'WARN'  },
    'WARNING':  { cls: 'lv-badge-warn',  label: 'WARN'  },
    'INFO':     { cls: 'lv-badge-info',  label: 'INFO'  },
    'DEBUG':    { cls: 'lv-badge-debug', label: 'DEBUG' },
    'TRACE':    { cls: 'lv-badge-trace', label: 'TRACE' },
  };

  function detectLevel(line) {
    for (const key of LEVEL_ORDER) {
      if (line.includes(`[${key}]`) || line.includes(` ${key} `) || line.includes(`:${key}:`))
        return key;
    }
    // uppercase scan
    const up = line.toUpperCase();
    for (const key of LEVEL_ORDER) {
      if (up.includes(key)) return key;
    }
    return 'INFO';
  }

  // ── parse raw log line ──────────────────────────────────────
  // Tries to extract timestamp from beginning: "2025-01-01 12:34:56.789 [INFO] msg"
  // or "[2025-01-01 12:34:56] [INFO] msg"
  const RE_TS = /^(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]?)\s*/;

  function parseLine(raw, daemonHint) {
    let time = '';
    let rest = raw;
    const m = raw.match(RE_TS);
    if (m) {
      time = m[2];
      rest = raw.slice(m[0].length);
    }
    const level = detectLevel(raw);
    // strip level tag from message
    let msg = rest
      .replace(/\[(CRITICAL|CRIT|ERROR|WARN(?:ING)?|INFO|DEBUG|TRACE)\]/gi, '')
      .trim();
    return { time, daemon: daemonHint, level, msg: msg || rest.trim(), raw };
  }

  // ── render filtered rows ────────────────────────────────────
  function escHtml(s) {
    return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  }

  function highlight(s, kw) {
    if (!kw) return escHtml(s);
    const re = new RegExp(`(${kw.replace(/[.*+?^${}()|[\]\\]/g,'\\$&')})`, 'gi');
    return escHtml(s).replace(re, '<mark class="lv-mark">$1</mark>');
  }

  function render() {
    const kw = searchKw.toLowerCase();
    const normLevel = activeLevel.toUpperCase();

    const filtered = allRows.filter(r => {
      if (normLevel) {
        const rl = r.level.toUpperCase();
        // CRIT filter matches CRITICAL/CRIT; WARN matches WARN/WARNING
        if (normLevel === 'CRIT'  && rl !== 'CRITICAL' && rl !== 'CRIT')   return false;
        if (normLevel === 'WARN'  && rl !== 'WARN'     && rl !== 'WARNING') return false;
        if (normLevel !== 'CRIT' && normLevel !== 'WARN' && rl !== normLevel) return false;
      }
      if (kw && !r.raw.toLowerCase().includes(kw)) return false;
      return true;
    });

    // stats
    document.getElementById('lv-count-total').textContent    = allRows.length.toLocaleString();
    document.getElementById('lv-count-filtered').textContent = filtered.length.toLocaleString();

    const tbody = document.getElementById('logTbody');
    if (!tbody) return;

    if (filtered.length === 0) {
      tbody.innerHTML = '<tr><td colspan="4" class="lv-placeholder">일치하는 로그가 없습니다.</td></tr>';
      return;
    }

    const rows = filtered.map(r => {
      const meta = LEVEL_META[r.level] || LEVEL_META['INFO'];
      const badge  = `<span class="lv-badge ${meta.cls}">${meta.label}</span>`;
      const timeCell = r.time
        ? `<span class="lv-time">${escHtml(r.time)}</span>`
        : '<span class="lv-time lv-muted">—</span>';
      const daemonCell = r.daemon
        ? `<span class="lv-daemon-chip">${escHtml(r.daemon)}</span>`
        : '';
      const msgCell = highlight(r.msg || r.raw, kw);
      return `<tr class="lv-row lv-row-${meta.cls.split('-')[2] || 'info'}">
        <td class="lv-td-lv">${badge}</td>
        <td class="lv-td-time">${timeCell}</td>
        <td class="lv-td-daemon">${daemonCell}</td>
        <td class="lv-td-msg">${msgCell}</td>
      </tr>`;
    });

    tbody.innerHTML = rows.join('');

    // auto-scroll to bottom
    const wrap = document.querySelector('.lv-table-wrap');
    if (wrap) wrap.scrollTop = wrap.scrollHeight;
  }

  // ── fetch ───────────────────────────────────────────────────
  async function load() {
    const lines  = parseInt(document.getElementById('logLines')?.value  || '200');
    const daemon = document.getElementById('daemonSelect')?.value || '';

    const url = daemon
      ? `/api/logs?daemon=${encodeURIComponent(daemon)}&lines=${lines}`
      : `/api/logs?lines=${lines}`;

    try {
      const data = await window.NMS.utils.fetchJSON(url);
      if (!data) return;

      allRows = [];

      if (daemon) {
        const rawLines = data.lines || [];
        allRows = rawLines.map(l => parseLine(l, daemon));
      } else {
        const daemons = data.daemons || {};
        for (const [d, dlines] of Object.entries(daemons)) {
          for (const l of dlines) {
            allRows.push(parseLine(l, d));
          }
        }
        // Sort best-effort by raw line (timestamp prefix tends to sort correctly)
        allRows.sort((a, b) => (a.time || a.raw) < (b.time || b.raw) ? -1 : 1);
      }

      render();
    } catch (e) {
      const tbody = document.getElementById('logTbody');
      if (tbody) tbody.innerHTML = `<tr><td colspan="4" class="lv-placeholder lv-err">로드 실패: ${escHtml(String(e))}</td></tr>`;
    }
  }

  // ── auto-refresh ─────────────────────────────────────────────
  function startAutoRefresh() {
    stopAutoRefresh();
    if (autoRefresh) refreshTimer = setInterval(load, REFRESH_MS);
  }
  function stopAutoRefresh() {
    if (refreshTimer) { clearInterval(refreshTimer); refreshTimer = null; }
  }

  // ── init ─────────────────────────────────────────────────────
  document.addEventListener('DOMContentLoaded', () => {

    // Refresh
    document.getElementById('refreshBtn')?.addEventListener('click', load);

    // Clear
    document.getElementById('clearBtn')?.addEventListener('click', () => {
      allRows = [];
      render();
    });

    // Lines
    document.getElementById('logLines')?.addEventListener('change', load);

    // Daemon
    document.getElementById('daemonSelect')?.addEventListener('change', load);

    // Search
    document.getElementById('logSearch')?.addEventListener('input', e => {
      searchKw = e.target.value.trim();
      render();
    });

    // Level filter tabs
    document.getElementById('levelFilter')?.addEventListener('click', e => {
      const btn = e.target.closest('.lv-level-tab');
      if (!btn) return;
      document.querySelectorAll('.lv-level-tab').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      activeLevel = btn.dataset.level;
      render();
    });

    // Auto-refresh toggle
    const autoChk = document.getElementById('autoRefresh');
    const dot      = document.getElementById('autoRefreshDot');
    const lbl      = document.getElementById('autoRefreshLabel');
    document.getElementById('autoToggle')?.addEventListener('click', () => {
      autoRefresh = !autoRefresh;
      autoChk.checked = autoRefresh;
      dot.classList.toggle('lv-dot-live', autoRefresh);
      lbl.textContent = autoRefresh ? 'Live' : 'Paused';
      autoRefresh ? startAutoRefresh() : stopAutoRefresh();
    });
    if (dot) dot.classList.add('lv-dot-live');

    load();
    startAutoRefresh();
  });
}());
