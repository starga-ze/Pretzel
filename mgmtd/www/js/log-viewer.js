/* log-viewer.js */
(function () {
  'use strict';

  // Service daemons exposed to users (ipcd / mgmtd excluded — infra only)
  const SERVICE_DAEMONS = ['engined', 'authd', 'icmpd', 'snmpd', 'topologyd'];

  let allRows     = [];
  let activeLevel = '';
  let searchKw    = '';

  // ── Level metadata ────────────────────────────────────────
  const LEVEL_ORDER = ['ERROR', 'WARN', 'WARNING', 'INFO', 'DEBUG', 'TRACE'];

  const LEVEL_META = {
    'ERROR':    { cls: 'lv-badge-error', label: 'ERROR' },
    'WARN':     { cls: 'lv-badge-warn',  label: 'WARN'  },
    'WARNING':  { cls: 'lv-badge-warn',  label: 'WARN'  },
    'INFO':     { cls: 'lv-badge-info',  label: 'INFO'  },
    'DEBUG':    { cls: 'lv-badge-debug', label: 'DEBUG' },
    'TRACE':    { cls: 'lv-badge-trace', label: 'TRACE' },
  };

  // ── ANSI escape code stripper ─────────────────────────────
  // Matches ESC [ ... m  and  ESC ] ... ST  and standalone ESC sequences
  const RE_ANSI = /\x1b\[[0-9;]*[mGKHFABCDJsur]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[=><NOMHFEl0-9]/g;

  function stripAnsi(s) {
    return s.replace(RE_ANSI, '');
  }

  // ── Log line parser ───────────────────────────────────────
  // Typical spdlog format:
  //   [2025-06-09 12:34:56.789] [pz-icmpd] [info] [ProbeService.cpp:347] sending ProbeResult …
  //
  // Parsed fields:
  //   time  → "2025-06-09 12:34:56.789"
  //   level → "INFO"
  //   loc   → "ProbeService.cpp:347"   (from the optional [file:line] bracket)
  //   msg   → "sending ProbeResult …"  (clean message body after the location tag)

  // Matches the spdlog file format:  [timestamp][LEVEL][file.cpp:line] message
  // Pattern: "[%Y-%m-%d %H:%M:%S.%e][%!][%s:%#] %v"  (%! = custom short-level flag)
  const RE_SPDLOG = /^\[(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]\s*\[(critical|error|warn(?:ing)?|info|debug|trace)\]\s*(?:\[([^\]]+\.\w+:\d+)\]\s*)?(.*)/i;

  // Fallback: location bracket anywhere in the line
  const RE_LOC_ANYWHERE = /\[([A-Za-z0-9_]+\.\w+:\d+)\]/;

  function detectLevel(line) {
    for (const key of LEVEL_ORDER) {
      if (line.includes(`[${key}]`) || line.includes(`[${key.toLowerCase()}]`)) return key;
    }
    const up = line.toUpperCase();
    for (const key of LEVEL_ORDER) {
      if (up.includes(key)) return key;
    }
    return 'INFO';
  }

  function parseLine(raw, daemonHint) {
    const clean = stripAnsi(raw);
    const m = clean.match(RE_SPDLOG);
    if (m) {
      const level = m[2].toUpperCase().replace('WARNING', 'WARN');
      const loc   = m[3] ? m[3].trim() : '';
      const msg   = m[4] ? m[4].trim() : '';
      return { time: m[1], daemon: daemonHint, level, loc, msg, raw: clean };
    }
    // Fallback: try to extract timestamp at start
    const RE_TS = /^(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]?)\s*/;
    const tm = clean.match(RE_TS);
    const time  = tm ? tm[2] : '';
    const rest  = tm ? clean.slice(tm[0].length) : clean;
    const level = detectLevel(clean);
    // Try to pull location from anywhere in the line
    const locM = rest.match(RE_LOC_ANYWHERE);
    const loc  = locM ? locM[1] : '';
    // Strip level tag and location bracket from message
    const msg = rest
      .replace(/\[(error|warn(?:ing)?|info|debug|trace|critical)\]/gi, '')
      .replace(RE_LOC_ANYWHERE, '')
      .trim();
    return { time, daemon: daemonHint, level, loc, msg: msg || rest, raw: clean };
  }

  // ── Rendering helpers ─────────────────────────────────────
  function escHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  function highlight(s, kw) {
    if (!kw) return escHtml(s);
    const re = new RegExp(`(${kw.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi');
    return escHtml(s).replace(re, '<mark class="lv-mark">$1</mark>');
  }

  // ── Progress bar helpers ──────────────────────────────────
  function showProgress(label) {
    const wrap = document.getElementById('lvProgressWrap');
    const fill = document.getElementById('lvProgressFill');
    const lbl  = document.getElementById('lvProgressLabel');
    if (wrap) wrap.style.display = 'flex';
    if (fill) fill.style.width = '0%';
    if (lbl)  lbl.textContent = label || 'Rendering…';
  }

  function setProgress(pct) {
    const fill = document.getElementById('lvProgressFill');
    if (fill) fill.style.width = `${Math.min(100, pct)}%`;
    const lbl = document.getElementById('lvProgressLabel');
    if (lbl) lbl.textContent = `Rendering… ${Math.round(pct)}%`;
  }

  function hideProgress() {
    const wrap = document.getElementById('lvProgressWrap');
    if (wrap) {
      const fill = document.getElementById('lvProgressFill');
      if (fill) fill.style.width = '100%';
      setTimeout(() => { wrap.style.display = 'none'; }, 300);
    }
  }

  // ── Chunked render to avoid UI freeze ────────────────────
  const CHUNK_SIZE = 200;

  function renderChunked(filtered, kw) {
    const tbody = document.getElementById('logTbody');
    if (!tbody) return;
    tbody.innerHTML = '';

    if (filtered.length === 0) {
      tbody.innerHTML = '<tr><td colspan="5" class="lv-placeholder">No matching log entries.</td></tr>';
      hideProgress();
      return;
    }

    showProgress('Rendering… 0%');

    let idx = 0;
    const total = filtered.length;

    function processChunk() {
      const end = Math.min(idx + CHUNK_SIZE, total);
      const frag = document.createDocumentFragment();

      for (; idx < end; idx++) {
        const r = filtered[idx];
        const meta = LEVEL_META[r.level] || LEVEL_META['INFO'];
        const tr = document.createElement('tr');
        tr.className = `lv-row lv-row-${meta.cls.split('-')[2] || 'info'}`;

        tr.innerHTML =
          `<td class="lv-td-lv"><span class="lv-badge ${meta.cls}">${meta.label}</span></td>` +
          `<td class="lv-td-time"><span class="lv-time">${r.time ? escHtml(r.time) : '<span class="lv-muted">—</span>'}</span></td>` +
          `<td class="lv-td-daemon">${r.daemon ? `<span class="lv-daemon-chip">${escHtml(r.daemon)}</span>` : ''}</td>` +
          `<td class="lv-td-loc">${r.loc ? `<span class="lv-loc">${escHtml(r.loc)}</span>` : '<span class="lv-muted">—</span>'}</td>` +
          `<td class="lv-td-msg">${highlight(r.msg || r.raw, kw)}</td>`;

        frag.appendChild(tr);
      }

      tbody.appendChild(frag);
      setProgress((idx / total) * 100);

      if (idx < total) {
        requestAnimationFrame(processChunk);
      } else {
        hideProgress();
        // Scroll to bottom
        const wrap = document.querySelector('.lv-table-wrap');
        if (wrap) wrap.scrollTop = wrap.scrollHeight;
      }
    }

    requestAnimationFrame(processChunk);
  }

  // ── Filter + render ───────────────────────────────────────
  function render() {
    const kw = searchKw.toLowerCase();
    const normLevel = activeLevel.toUpperCase();

    const filtered = allRows.filter(r => {
      if (normLevel) {
        const rl = r.level.toUpperCase();
        if (normLevel === 'WARN' && rl !== 'WARN' && rl !== 'WARNING') return false;
        if (normLevel !== 'WARN' && rl !== normLevel) return false;
      }
      if (kw && !r.raw.toLowerCase().includes(kw)) return false;
      return true;
    });

    document.getElementById('lv-count-total').textContent    = allRows.length.toLocaleString();
    document.getElementById('lv-count-filtered').textContent = filtered.length.toLocaleString();

    renderChunked(filtered, kw);
  }

  // ── Refresh button spin animation ─────────────────────────
  function setRefreshing(on) {
    const btn = document.getElementById('refreshBtn');
    if (!btn) return;
    btn.classList.toggle('lv-refreshing', on);
    btn.disabled = on;
  }

  // ── Fetch ─────────────────────────────────────────────────
  async function load() {
    setRefreshing(true);

    const lines  = parseInt(document.getElementById('logLines')?.value  || '200');
    const daemon = document.getElementById('daemonSelect')?.value || '';

    // Only fetch service daemons (never ipcd / mgmtd)
    const fetchDaemons = daemon ? [daemon] : SERVICE_DAEMONS;

    try {
      if (daemon) {
        // Single daemon
        const data = await window.NMS.utils.fetchJSON(`/api/logs?daemon=${encodeURIComponent(daemon)}&lines=${lines}`);
        if (!data) return;
        allRows = (data.lines || []).map(l => parseLine(l, daemon));
      } else {
        // All service daemons in parallel
        const results = await Promise.all(
          fetchDaemons.map(d =>
            window.NMS.utils.fetchJSON(`/api/logs?daemon=${encodeURIComponent(d)}&lines=${lines}`)
              .then(data => ({ d, lines: data?.lines || [] }))
              .catch(() => ({ d, lines: [] }))
          )
        );
        allRows = [];
        for (const { d, lines: dlines } of results) {
          for (const l of dlines) allRows.push(parseLine(l, d));
        }
        allRows.sort((a, b) => (a.time || a.raw) < (b.time || b.raw) ? -1 : 1);
      }

      render();
    } catch (e) {
      const tbody = document.getElementById('logTbody');
      if (tbody) tbody.innerHTML = `<tr><td colspan="5" class="lv-placeholder lv-err">Failed to load: ${escHtml(String(e))}</td></tr>`;
    } finally {
      setRefreshing(false);
    }
  }

  // ── Init ──────────────────────────────────────────────────
  document.addEventListener('DOMContentLoaded', () => {

    document.getElementById('refreshBtn')?.addEventListener('click', load);
    document.getElementById('logLines')?.addEventListener('change', load);
    document.getElementById('daemonSelect')?.addEventListener('change', load);

    document.getElementById('logSearch')?.addEventListener('input', e => {
      searchKw = e.target.value.trim();
      render();
    });

    document.getElementById('levelFilter')?.addEventListener('click', e => {
      const btn = e.target.closest('.lv-level-tab');
      if (!btn) return;
      document.querySelectorAll('.lv-level-tab').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      activeLevel = btn.dataset.level;
      render();
    });

    load();
  });
}());
