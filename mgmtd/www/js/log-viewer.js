/* log-viewer.js */
(function () {
  'use strict';

  // Service daemons (ipcd / mgmtd excluded — infra only)
  const SERVICE_DAEMONS = ['engined', 'authd', 'icmpd', 'snmpd', 'topologyd'];

  // Max lines fetched per daemon — server reads tail of file
  const FETCH_LINES = 2000;

  const PAGE_SIZE = 30;   // rows per page

  let allRows      = [];
  let filtered     = [];
  let activeLevel  = '';
  let activeDaemon = '';
  let searchKw     = '';
  let currentPage  = 1;

  // ── Level metadata ────────────────────────────────────────
  const LEVEL_ORDER = ['ERROR', 'WARN', 'WARNING', 'INFO', 'DEBUG', 'TRACE'];

  const LEVEL_META = {
    'ERROR':   { cls: 'lv-badge-error', label: 'ERROR', dot: '#ef4444' },
    'WARN':    { cls: 'lv-badge-warn',  label: 'WARN',  dot: '#f59e0b' },
    'WARNING': { cls: 'lv-badge-warn',  label: 'WARN',  dot: '#f59e0b' },
    'INFO':    { cls: 'lv-badge-info',  label: 'INFO',  dot: '#22c55e' },
    'DEBUG':   { cls: 'lv-badge-debug', label: 'DEBUG', dot: '#3b82f6' },
    'TRACE':   { cls: 'lv-badge-trace', label: 'TRACE', dot: '#8b5cf6' },
  };

  // ── ANSI stripper ─────────────────────────────────────────
  const RE_ANSI = /\x1b\[[0-9;]*[mGKHFABCDJsur]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[=><NOMHFEl0-9]/g;
  function stripAnsi(s) { return s.replace(RE_ANSI, ''); }

  // ── Log line parser ───────────────────────────────────────
  const RE_SPDLOG = /^\[(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]\s*\[(critical|error|warn(?:ing)?|info|debug|trace)\]\s*(?:\[([^\]]+\.\w+:\d+)\]\s*)?(.*)/i;
  const RE_LOC_ANYWHERE = /\[([A-Za-z0-9_]+\.\w+:\d+)\]/;

  function detectLevel(line) {
    for (const key of LEVEL_ORDER) {
      if (line.includes(`[${key}]`) || line.includes(`[${key.toLowerCase()}]`)) return key;
    }
    const up = line.toUpperCase();
    for (const key of LEVEL_ORDER) { if (up.includes(key)) return key; }
    return 'INFO';
  }

  function parseLine(raw, daemonHint) {
    const clean = stripAnsi(raw);
    const m = clean.match(RE_SPDLOG);
    if (m) {
      const level = m[2].toUpperCase().replace('WARNING', 'WARN');
      return { time: m[1], daemon: daemonHint, level, loc: (m[3] || '').trim(), msg: (m[4] || '').trim(), raw: clean };
    }
    const RE_TS = /^(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?)\]?)\s*/;
    const tm   = clean.match(RE_TS);
    const time = tm ? tm[2] : '';
    const rest = tm ? clean.slice(tm[0].length) : clean;
    const level = detectLevel(clean);
    const locM = rest.match(RE_LOC_ANYWHERE);
    const loc  = locM ? locM[1] : '';
    const msg  = rest.replace(/\[(error|warn(?:ing)?|info|debug|trace|critical)\]/gi, '').replace(RE_LOC_ANYWHERE, '').trim();
    return { time, daemon: daemonHint, level, loc, msg: msg || rest, raw: clean };
  }

  // ── Helpers ───────────────────────────────────────────────
  function escHtml(s) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
  function highlight(s, kw) {
    if (!kw) return escHtml(s);
    const re = new RegExp(`(${kw.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi');
    return escHtml(s).replace(re, '<mark class="lv-mark">$1</mark>');
  }
  function pad2(n) { return String(n).padStart(2, '0'); }

  // ── Loading overlay ───────────────────────────────────────
  function showLoading(label) {
    const o = document.getElementById('lvLoadingOverlay');
    const l = document.getElementById('lvLoadingLabel');
    if (l) l.textContent = label || 'Loading…';
    if (o) o.classList.add('visible');
  }
  function setLoadingLabel(label) {
    const l = document.getElementById('lvLoadingLabel');
    if (l) l.textContent = label;
  }
  function hideLoading() {
    document.getElementById('lvLoadingOverlay')?.classList.remove('visible');
  }

  // ── Filter ────────────────────────────────────────────────
  function applyFilter() {
    const kw        = searchKw.toLowerCase();
    const normLevel = activeLevel.toUpperCase();

    filtered = allRows.filter(r => {
      // Skip lines that have no timestamp, no location, and no meaningful message
      if (!r.time && !r.loc && (!r.msg || r.msg.trim() === '')) return false;
      if (activeDaemon && r.daemon !== activeDaemon) return false;
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

    currentPage = 1;
    renderPage();
  }

  // ── Render current page ───────────────────────────────────
  function renderPage() {
    const kw    = searchKw.toLowerCase();
    const tbody = document.getElementById('logTbody');
    if (!tbody) return;

    const total = filtered.length;
    const pages = Math.max(1, Math.ceil(total / PAGE_SIZE));
    if (currentPage > pages) currentPage = pages;
    if (currentPage < 1)     currentPage = 1;

    const start = (currentPage - 1) * PAGE_SIZE;
    const slice = filtered.slice(start, start + PAGE_SIZE);

    if (slice.length === 0) {
      tbody.innerHTML = '<tr><td colspan="5" class="lv-placeholder">No matching log entries.</td></tr>';
    } else {
      const frag = document.createDocumentFragment();
      for (const r of slice) {
        const meta = LEVEL_META[r.level] || LEVEL_META['INFO'];
        const tr   = document.createElement('tr');
        tr.className = `lv-row lv-row-${meta.cls.split('-')[2] || 'info'}`;
        tr.innerHTML =
          `<td class="lv-td-lv"><span class="lv-badge ${meta.cls}">${meta.label}</span></td>` +
          `<td class="lv-td-daemon">${r.daemon ? `<span class="lv-daemon-chip">${escHtml(r.daemon)}</span>` : ''}</td>` +
          `<td class="lv-td-time"><span class="lv-time">${r.time ? escHtml(r.time) : '<span class="lv-muted">—</span>'}</span></td>` +
          `<td class="lv-td-loc">${r.loc ? `<span class="lv-loc">${escHtml(r.loc)}</span>` : '<span class="lv-muted">—</span>'}</td>` +
          `<td class="lv-td-msg">${highlight(r.msg || r.raw, kw)}</td>`;
        frag.appendChild(tr);
      }
      tbody.innerHTML = '';
      tbody.appendChild(frag);
    }

    const wrap = document.querySelector('.lv-table-wrap');
    if (wrap) wrap.scrollTop = 0;

    renderPagination(pages);
  }

  // ── Pagination bar ─────────────────────────────────────────
  function renderPagination(pages) {
    const bar = document.getElementById('lvPagination');
    if (!bar) return;
    bar.innerHTML = '';
    if (pages <= 1) return;

    function navBtn(label, title, clickFn, disabled) {
      const b = document.createElement('button');
      b.className   = 'lv-page-btn lv-page-nav';
      b.textContent = label;
      b.title       = title;
      b.disabled    = disabled;
      if (!disabled) b.addEventListener('click', clickFn);
      return b;
    }

    bar.appendChild(navBtn('<<<', 'First page',  () => { currentPage = 1; renderPage(); }, currentPage === 1));
    bar.appendChild(navBtn('<<',  '−10 pages',   () => { currentPage = Math.max(1, currentPage - 10); renderPage(); }, currentPage <= 1));
    bar.appendChild(navBtn('<',   'Previous',    () => { currentPage = Math.max(1, currentPage - 1); renderPage(); }, currentPage === 1));

    const inputWrap = document.createElement('span');
    inputWrap.className = 'lv-page-input-wrap';

    const inp = document.createElement('input');
    inp.type      = 'number';
    inp.className = 'lv-page-input';
    inp.min       = 1;
    inp.max       = pages;
    inp.value     = currentPage;
    inp.addEventListener('change', () => {
      let v = parseInt(inp.value);
      if (isNaN(v) || v < 1) v = 1;
      if (v > pages) v = pages;
      currentPage = v;
      inp.value = currentPage;
      renderPage();
    });
    inp.addEventListener('keydown', e => { if (e.key === 'Enter') inp.dispatchEvent(new Event('change')); });

    const totalSpan = document.createElement('span');
    totalSpan.className   = 'lv-page-total';
    totalSpan.textContent = `/ ${pages}`;

    inputWrap.appendChild(inp);
    inputWrap.appendChild(totalSpan);
    bar.appendChild(inputWrap);

    bar.appendChild(navBtn('>',   'Next',        () => { currentPage = Math.min(pages, currentPage + 1);  renderPage(); }, currentPage === pages));
    bar.appendChild(navBtn('>>',  '+10 pages',   () => { currentPage = Math.min(pages, currentPage + 10); renderPage(); }, currentPage >= pages));
    bar.appendChild(navBtn('>>>', 'Last page',   () => { currentPage = pages; renderPage(); }, currentPage === pages));
  }

  // ── Custom dropdown ───────────────────────────────────────
  // Generic factory: wraps a trigger button + list of items.
  // onChange(value, label) called on selection.
  function initDropdown(wrapId, btnId, listId, onChange) {
    const wrap = document.getElementById(wrapId);
    const btn  = document.getElementById(btnId);
    const list = document.getElementById(listId);
    if (!wrap || !btn || !list) return;

    function open() {
      // Close all other dropdowns first
      document.querySelectorAll('.lv-dd-list.lv-dd-open').forEach(el => {
        if (el !== list) el.classList.remove('lv-dd-open');
      });
      document.querySelectorAll('.lv-dd-btn.lv-dd-active').forEach(el => {
        if (el !== btn) el.classList.remove('lv-dd-active');
      });
      list.classList.toggle('lv-dd-open');
      btn.classList.toggle('lv-dd-active');

      // Smart positioning: flip upward if not enough space below
      if (list.classList.contains('lv-dd-open')) {
        list.classList.remove('lv-dd-up');
        const rect = list.getBoundingClientRect();
        if (rect.bottom > window.innerHeight - 8) list.classList.add('lv-dd-up');
      }
    }

    function close() {
      list.classList.remove('lv-dd-open');
      btn.classList.remove('lv-dd-active');
    }

    btn.addEventListener('click', e => { e.stopPropagation(); open(); });

    list.querySelectorAll('.lv-dd-item').forEach(item => {
      item.addEventListener('click', () => {
        list.querySelectorAll('.lv-dd-item').forEach(i => i.classList.remove('lv-dd-selected'));
        item.classList.add('lv-dd-selected');
        onChange(item.dataset.value, item.textContent.trim());
        close();
      });
    });

    document.addEventListener('click', e => {
      if (!wrap.contains(e.target)) close();
    });

    document.addEventListener('keydown', e => {
      if (e.key === 'Escape') close();
    });
  }

  // ── Level dot indicator ───────────────────────────────────
  function updateLevelDot(level) {
    const dot = document.getElementById('lvLevelDot');
    if (!dot) return;
    const meta = LEVEL_META[level];
    if (meta) {
      dot.style.background = meta.dot;
      dot.style.display = 'inline-block';
    } else {
      dot.style.display = 'none';
    }
  }

  // ── Refresh spin ───────────────────────────────────────────
  function setRefreshing(on) {
    const btn = document.getElementById('refreshBtn');
    if (!btn) return;
    btn.classList.toggle('lv-refreshing', on);
    btn.disabled = on;
  }

  // ── Fetch — tail 2000 lines per daemon ────────────────────
  async function load() {
    setRefreshing(true);
    showLoading('Loading…');
    setLoadingLabel(`Loading ${SERVICE_DAEMONS.length} services…`);

    try {
      const results = await Promise.all(
        SERVICE_DAEMONS.map(d =>
          window.NMS.utils.fetchJSON(`/api/logs?daemon=${encodeURIComponent(d)}&lines=${FETCH_LINES}`)
            .then(data => ({ d, lines: data?.lines || [] }))
            .catch(() => ({ d, lines: [] }))
        )
      );
      allRows = [];
      for (const { d, lines } of results)
        for (const l of lines) allRows.push(parseLine(l, d));
      // Newest first (descending)
      allRows.sort((a, b) => (a.time || a.raw) > (b.time || b.raw) ? -1 : 1);

      setLoadingLabel('Rendering…');
      await new Promise(r => requestAnimationFrame(r));
      applyFilter();
    } catch (e) {
      const tbody = document.getElementById('logTbody');
      if (tbody) tbody.innerHTML = `<tr><td colspan="5" class="lv-placeholder lv-err">Failed to load: ${escHtml(String(e))}</td></tr>`;
    } finally {
      hideLoading();
      setRefreshing(false);
    }
  }

  // ── Init ──────────────────────────────────────────────────
  document.addEventListener('DOMContentLoaded', () => {
    // Service dropdown
    initDropdown('serviceDropWrap', 'serviceDropBtn', 'serviceDropList', (value, label) => {
      document.getElementById('serviceDropLabel').textContent = label;
      activeDaemon = value;
      applyFilter();
    });

    // Level dropdown
    initDropdown('levelDropWrap', 'levelDropBtn', 'levelDropList', (value, label) => {
      // Update label (strip dot text if present)
      const cleanLabel = value
        ? document.querySelector(`#levelDropList [data-value="${value}"]`)?.textContent.trim() || label
        : 'All Levels';
      document.getElementById('levelDropLabel').textContent = cleanLabel;
      activeLevel = value;
      updateLevelDot(value);
      applyFilter();
    });

    document.getElementById('refreshBtn')?.addEventListener('click', load);

    document.getElementById('logSearch')?.addEventListener('input', e => {
      searchKw = e.target.value.trim();
      applyFilter();
    });

    load();
  });
}());
