/* advisories.js — Knowledge › Security Advisories
 *
 * What this page is for
 * ---------------------
 * Tech Documentation is a CATALOGUE: everything the assistant can read, browsed product by
 * product, and the operator arrives there to look something up. This page is a QUEUE. The operator
 * arrives because something may need patching, and the only question that matters is "what do I
 * have to act on, and has it changed since I last looked".
 *
 * Why it is drawn on a canvas
 * ---------------------------
 * Same reasoning as Site Infrastructure, and the same surface: this is a page you WATCH, not a page
 * you change. The light configuration pages are where the estate is described; the dark canvas is
 * where it is read. An advisory list belongs on the second kind of surface — it is a feed of things
 * happening to you — so it borrows topology's canvas, its glass cards and its scoped token set
 * rather than the flat admin table the rest of Knowledge uses. See advisories.css.
 *
 * Why the severity bar replaced a row of chips
 * --------------------------------------------
 * A row of filter chips with counts on them answers "how many are critical" one chip at a time and
 * never answers "how bad is this estate's week". One stacked bar answers both: the proportion is
 * the picture, the legend under it carries the same counts, and the legend IS the filter — so the
 * control and the readout are one object instead of two that must be kept in agreement.
 *
 * Why "updated" is a first-class state
 * ------------------------------------
 * An advisory is not published once. Measured against the upstream feed, one in four is revised
 * after publication (median 22 days, with a tail past four years) — a CVSS score is re-scored, a
 * fixed release finally ships, the affected range widens. So the row carries a revision mark, the
 * list can be narrowed to what moved recently, and the drawer prints both dates rather than one.
 *
 * Deep links
 * ----------
 * The point of collecting advisories is the alert mail, and the mail has to land somewhere. So
 * ?id=<CVE> opens that advisory's drawer directly on load, and ?severity=critical pre-filters the
 * list. Both are reflected back into the URL, so a view is linkable and Back steps through drawers
 * instead of leaving the page.
 *
 * Rendering
 * ---------
 * The shell is built once and then patched region by region. Tech Docs rewrites #contentBody
 * wholesale and pays for it with caret-restore and scroll-restore bookkeeping on every keystroke;
 * with a search box, a drawer that animates and a list that repaints under both, that does not
 * survive contact. Here the only thing a fetch replaces is the list body.
 *
 * Server contract (mgmtd reads the advisory tables directly, as CollectionController does for
 * api_collection — engined stays the sole writer):
 *
 *   GET /api/advisory/overview
 *       { counts:{total,critical,high,medium,low,none}, updated_30d,
 *         last_sync, last_sync_status, newest }
 *   GET /api/advisory/list?severity=&product=&q=&window=&sort=&offset=&limit=
 *       { total, offset, limit, products:[], items:[ {id,title,severity,base_score,
 *         published,updated,revised,products:[],has_fix} ] }
 *   GET /api/advisory/detail?id=<id>
 *       { id,title,severity,base_score,threat_score,published,updated,url,aliases:[],
 *         cvss:{AV,AC,PR,UI,C,I,A}, versions:[{product,affected,fixed}],
 *         problem,solution,work_around }
 */
(function () {
  'use strict';

  const { esc } = window.NMS.utils;

  const PAGE = 25;
  const MIN_QUERY = 3;

  // Ordered worst-first: it is the order the bar stacks in, the order the legend reads in, and the
  // tie-break of the severity sort. `none` is real — a portfolio-status advisory can carry no CVSS.
  const SEVERITIES = ['critical', 'high', 'medium', 'low', 'none'];
  const SEV_LABEL = { critical:'Critical', high:'High', medium:'Medium', low:'Low', none:'Unscored' };

  // The two scores an advisory carries are not interchangeable — base is the vulnerability, threat
  // is base adjusted for what is happening in the wild — but only one can be the number in the
  // gauge. Base, because every advisory has it and the vendor's severity word derives from it.
  const CVSS_LABEL = {
    AV: 'Attack vector', AC: 'Attack complexity', PR: 'Privileges required',
    UI: 'User interaction', C: 'Confidentiality', I: 'Integrity', A: 'Availability',
  };

  const WINDOWS = [
    { id: '',    label: 'Any time'    },
    { id: '7',   label: 'Last 7 days' },
    { id: '30',  label: 'Last 30 days'},
    { id: '90',  label: 'Last 90 days'},
    { id: '365', label: 'Last year'   },
  ];

  const SORTS = [
    { id: 'updated',  label: 'Recently revised' },
    { id: 'published',label: 'Newest'           },
    { id: 'severity', label: 'Most severe'      },
  ];

  const state = {
    overview: null, overviewError: '', overviewLoading: false, refreshing: false,
    severity: '', product: '', q: '', window: '', sort: 'updated',
    items: [], total: 0, offset: 0, products: [],
    listLoading: false, listError: '',
    openId: '', detail: null, detailError: '', detailLoading: false, tab: 'overview',
  };

  let listSeq = 0, detailSeq = 0, searchTimer = null, lastFocus = null;

  const num  = n => Number(n || 0).toLocaleString();
  const byId = id => document.getElementById(id);

  // A severity word we did not expect must not silently style itself as `none` — it is more likely
  // a feed change than a new nothing-severity, and it should be visible as itself.
  const sevClass = s => SEVERITIES.includes(String(s || '').toLowerCase())
    ? String(s).toLowerCase() : 'none';

  const hasScore = v => v !== null && v !== undefined && v !== '' && !isNaN(Number(v));
  const score1   = v => hasScore(v) ? Number(v).toFixed(1) : '—';

  // Through NMS.utils, never `new Date` or toLocaleDateString: mgmtd prints a whole-hour zone the
  // way Postgres does, as `+09` rather than `+09:00`, which Date rejects outright — and a locale
  // format would render the same appliance differently to two operators (see the note on fmtTs).
  // One shape everywhere: YYYY-MM-DD HH:MM:SS, cut to the day where the time says nothing.
  //
  // fmtTs is called through the object rather than destructured: it reaches parseTs via `this`.
  const parseTs = x => window.NMS.utils.parseTs(x);
  const stampTs = x => window.NMS.utils.fmtTs(x);
  const dayTs   = x => { const s = stampTs(x); return s === '—' ? '—' : s.slice(0, 10); };

  // Dates on a row are absolute. They were relative ("2d ago") and read well for the top of the
  // list and badly for the rest of it — most of this catalogue is years old, so "4y ago" was the
  // answer to a question nobody asked while the date an operator needs to put in a ticket was
  // nowhere on screen. Relative survives only in the drawer, next to the absolute date it
  // qualifies.
  function relDate(x) {
    const d = x && parseTs(x);
    if (!d) return '—';
    const days = Math.floor((Date.now() - d.getTime()) / 86400000);
    if (days < 0)   return dayTs(x);
    if (days === 0) return 'today';
    if (days === 1) return 'yesterday';
    if (days < 30)  return days + 'd ago';
    if (days < 365) return Math.floor(days / 30) + 'mo ago';
    return Math.floor(days / 365) + 'y ago';
  }

  async function api(url, method) {
    const r = await fetch(url, { method: method || 'GET', credentials: 'same-origin',
                                 signal: AbortSignal.timeout(15000) });
    if (r.status === 401) { location.href = '/'; throw Error('Session expired.'); }
    // The collector ships after this page does, so a 404 here is a build without the feature rather
    // than a fault. Saying "Request failed (404)" to that would send an operator hunting a problem
    // that is not theirs.
    if (r.status === 404) { const e = Error('Advisory collection is not available on this appliance yet.'); e.absent = true; throw e; }
    const data = await r.json().catch(() => ({}));
    if (!r.ok || data.error) throw Error(data.error || `Request failed (${r.status}).`);
    return data;
  }

  // ── URL ──────────────────────────────────────────────────────────────────
  // The filters an operator would send to someone else, and nothing else: page offset and the open
  // drawer tab are view state, not an address.

  function syncUrl(push) {
    const p = new URLSearchParams();
    if (state.severity)           p.set('severity', state.severity);
    if (state.product)            p.set('product', state.product);
    if (state.q.trim())           p.set('q', state.q.trim());
    if (state.window)             p.set('window', state.window);
    if (state.sort !== 'updated') p.set('sort', state.sort);
    if (state.openId)             p.set('id', state.openId);
    const url = location.pathname + (p.toString() ? '?' + p : '');
    if (url === location.pathname + location.search) return;
    history[push ? 'pushState' : 'replaceState'](null, '', url);
  }

  function readUrl() {
    const p = new URLSearchParams(location.search);
    const sev = (p.get('severity') || '').toLowerCase();
    state.severity = SEVERITIES.includes(sev) ? sev : '';
    state.product  = p.get('product') || '';
    state.q        = p.get('q') || '';
    state.window   = WINDOWS.some(w => w.id === p.get('window')) ? p.get('window') : '';
    state.sort     = SORTS.some(s => s.id === p.get('sort')) ? p.get('sort') : 'updated';
    state.openId   = p.get('id') || '';
  }

  // ── Shell ────────────────────────────────────────────────────────────────

  function shell() {
    const el = byId('contentBody');
    if (!el) return;
    el.innerHTML = `
      <div class="adv-page">
        <section class="adv-canvas">
          <header class="adv-head" id="advHead"></header>
          <div class="adv-filters" id="advFilters"></div>
          <div id="advNotice"></div>
          <div class="adv-list" id="advList" aria-busy="false"></div>
          <div class="adv-pager" id="advPager"></div>
        </section>
      </div>

      <div class="adv-drawer-overlay" id="advOverlay"></div>
      <aside class="adv-drawer" id="advDrawer" role="dialog" aria-modal="true"
             aria-labelledby="advDrawerT" tabindex="-1">
        <div class="adv-drawer-h">
          <div class="adv-gauge-slot" id="advDrawerGauge"></div>
          <div class="adv-drawer-id">
            <div class="adv-drawer-t" id="advDrawerT">Advisory</div>
            <div class="adv-drawer-sub" id="advDrawerS"></div>
          </div>
          <button class="adv-drawer-x" id="advDrawerX" type="button" aria-label="Close">&times;</button>
        </div>
        <div class="adv-tabs" id="advTabs" role="tablist"></div>
        <div class="adv-drawer-b" id="advDrawerB"></div>
      </aside>`;

    byId('advDrawerX').onclick = () => closeDrawer();
    byId('advOverlay').onclick = () => closeDrawer();
  }

  // ── Gauge ────────────────────────────────────────────────────────────────
  // A CVSS score is a 0–10 magnitude, and a number alone throws that away — 9.2 and 4.1 are the
  // same four pixels of ink. The ring draws the magnitude and the hue carries the band, so the
  // whole severity of a row is legible before any of its text is.

  function gauge(severity, value, big) {
    const sev = sevClass(severity);
    const pct = hasScore(value) ? Math.max(0, Math.min(10, Number(value))) * 10 : 0;
    return `<span class="adv-gauge${big ? ' big' : ''} sev-${sev}" style="--pct:${pct}">
      <span class="adv-ring" aria-hidden="true"></span>
      <span class="adv-gauge-n">${esc(score1(value))}</span></span>`;
  }

  // ── Head: headline count, severity distribution, sync state ──────────────

  function paintHead() {
    const el = byId('advHead'); if (!el) return;
    const o = state.overview, c = o?.counts || {};
    const total = Number(c.total) || 0;

    // Segments carry a floor so a single critical in six hundred is still a visible sliver rather
    // than a sub-pixel the browser rounds away — the one advisory that matters most is exactly the
    // one a proportional bar would hide.
    const present = SEVERITIES.filter(s => Number(c[s]) > 0);
    const bar = total && present.length
      ? present.map(s => {
          const pct = Math.max(2.5, (Number(c[s]) / total) * 100);
          return `<span class="adv-seg sev-${s}" style="flex:${pct}"
                        title="${esc(SEV_LABEL[s])}: ${num(c[s])}"></span>`;
        }).join('')
      : '<span class="adv-seg empty" style="flex:1"></span>';

    const legend = SEVERITIES.map(s => `
      <button class="adv-leg${state.severity === s ? ' on' : ''} sev-${s}" type="button"
              data-sev="${s}" aria-pressed="${state.severity === s}"
              ${o && !Number(c[s]) ? 'disabled' : ''}>
        <span class="adv-leg-dot" aria-hidden="true"></span>
        <span class="adv-leg-k">${esc(SEV_LABEL[s])}</span>
        <span class="adv-leg-n">${o ? num(c[s]) : '—'}</span>
      </button>`).join('');

    const syncOk = !o?.last_sync_status || o.last_sync_status === 'ok';

    el.innerHTML = `
      <div class="adv-head-top">
        <div class="adv-head-title">
          <h1 class="adv-h1">Security Advisories</h1>
          <p class="adv-h1-sub">Published vulnerabilities and fixes for Palo Alto Networks products</p>
        </div>
        <div class="adv-head-side">
          <span class="adv-sync${state.refreshing ? ' busy' : syncOk ? '' : ' warn'}"
                title="${o && o.last_sync_status ? esc('Last collection: ' + o.last_sync_status) : 'Never collected'}">
            <span class="adv-sync-dot" aria-hidden="true"></span>
            ${state.refreshing
              ? '<span class="adv-sync-k">Collecting…</span>'
              : o && o.last_sync
                ? `<span class="adv-sync-k">Last updated at</span><span class="adv-sync-v">${esc(stampTs(o.last_sync))}</span>`
                : '<span class="adv-sync-k">Never updated</span>'}
          </span>
          <button class="adv-icon-btn${state.refreshing ? ' spin' : ''}" id="advRefresh" type="button"
                  ${state.refreshing ? 'disabled' : ''}
                  title="${state.refreshing ? 'Collecting from the vendor feed…' : 'Collect from the vendor feed now'}"
                  aria-label="Refresh">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <polyline points="21 4 21 10 15 10"/>
              <path d="M19.4 14.5A8 8 0 1 1 18 7.3L21 10"/></svg>
          </button>
        </div>
      </div>

      <div class="adv-readout">
        <div class="adv-total">
          <span class="adv-total-n">${o ? num(total) : '—'}</span>
          <span class="adv-total-k">advisories</span>
        </div>
        <div class="adv-dist">
          <div class="adv-bar" role="img"
               aria-label="${o ? esc(SEVERITIES.filter(s => Number(c[s]) > 0)
                 .map(s => `${SEV_LABEL[s]} ${c[s]}`).join(', ')) : 'No data'}">${bar}</div>
          <div class="adv-legend" role="group" aria-label="Filter by severity">${legend}</div>
        </div>
        <div class="adv-revised${o && Number(o.updated_30d) ? ' on' : ''}">
          <span class="adv-revised-n">${o ? num(o.updated_30d) : '—'}</span>
          <span class="adv-revised-k">revised in 30d</span>
        </div>
      </div>`;

    byId('advRefresh').onclick = refreshNow;
    el.querySelectorAll('[data-sev]').forEach(b => { b.onclick = () => {
      // A second press on the active band clears it — the legend is the only severity control on
      // the page, so it has to be able to say "all" as well as "this one".
      state.severity = state.severity === b.dataset.sev ? '' : b.dataset.sev;
      syncUrl(false); paintHead(); loadList(0);
    }; });
  }

  // ── Filter strip ─────────────────────────────────────────────────────────

  function paintFilters() {
    const el = byId('advFilters'); if (!el) return;
    el.innerHTML = `
      <div class="adv-search-wrap">
        <svg class="adv-search-ic" viewBox="0 0 24 24" fill="none" stroke="currentColor"
             stroke-width="2" stroke-linecap="round" aria-hidden="true">
          <circle cx="11" cy="11" r="7"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
        <input class="adv-search" id="advSearch" type="search" maxlength="256" autocomplete="off"
               placeholder="Search CVE ID or title…" value="${esc(state.q)}"
               aria-label="Search advisories by CVE ID or title">
      </div>
      <div class="adv-filters-spacer"></div>
      <select class="adv-select" id="advProduct" aria-label="Filter by product">
        <option value="">All products</option>
        ${state.products.map(p =>
          `<option value="${esc(p)}"${state.product === p ? ' selected' : ''}>${esc(p)}</option>`).join('')}
      </select>
      <select class="adv-select narrow" id="advWindow" aria-label="Filter by recency">
        ${WINDOWS.map(w => `<option value="${w.id}"${state.window === w.id ? ' selected' : ''}>${esc(w.label)}</option>`).join('')}
      </select>
      <select class="adv-select narrow" id="advSort" aria-label="Sort order">
        ${SORTS.map(s => `<option value="${s.id}"${state.sort === s.id ? ' selected' : ''}>${esc(s.label)}</option>`).join('')}
      </select>`;

    byId('advProduct').onchange = e => { state.product = e.target.value; syncUrl(false); loadList(0); };
    byId('advWindow').onchange  = e => { state.window  = e.target.value; syncUrl(false); loadList(0); };
    byId('advSort').onchange    = e => { state.sort    = e.target.value; syncUrl(false); loadList(0); };

    byId('advSearch').oninput = e => {
      state.q = e.target.value;
      clearTimeout(searchTimer);
      const term = state.q.trim();
      // Below the floor the server is not asked, but an emptied box is a real query — it means
      // "drop the search", and it has to reach the list.
      if (term && term.length < MIN_QUERY) { paintList(); return; }
      searchTimer = setTimeout(() => { syncUrl(false); loadList(0); }, 300);
    };
  }

  // The product list arrives with the first page of results, which can land while the operator is
  // already typing in the search box beside it. Rebuilding the strip then would take their focus
  // and their caret, so only the <select> is replaced.
  function patchProducts() {
    const sel = byId('advProduct'); if (!sel) return;
    sel.innerHTML = '<option value="">All products</option>' + state.products.map(p =>
      `<option value="${esc(p)}"${state.product === p ? ' selected' : ''}>${esc(p)}</option>`).join('');
  }

  // ── List ─────────────────────────────────────────────────────────────────

  function row(a) {
    const sev = sevClass(a.severity);
    const products = Array.isArray(a.products) ? a.products : [];
    // Three is what fits on one line at the narrowest width this row is drawn at; the rest are
    // counted rather than wrapped, because a row that grows to five lines stops being scannable.
    const shown = products.slice(0, 3).map(p => `<span class="adv-prod">${esc(p)}</span>`).join('');
    const more  = products.length > 3 ? `<span class="adv-prod more">+${products.length - 3}</span>` : '';
    const open  = state.openId === a.id;

    return `<button class="adv-row sev-${sev}${open ? ' open' : ''}" type="button"
                    data-id="${esc(a.id)}" aria-expanded="${open}">
      ${gauge(a.severity, a.base_score)}
      <span class="adv-row-main">
        <span class="adv-row-top">
          <span class="adv-cve">${esc(a.id)}</span>
          <span class="adv-sev-tag sev-${sev}">${esc(SEV_LABEL[sev])}</span>
          ${a.revised ? `<span class="adv-tag revised" title="Content changed after publication">Revised</span>` : ''}
          ${a.has_fix ? `<span class="adv-tag fix" title="A fixed release is listed">Fix available</span>` : ''}
        </span>
        <span class="adv-title">${esc(a.title || '')}</span>
        <span class="adv-row-meta">${shown}${more}</span>
      </span>
      <span class="adv-row-when">
        <span class="adv-when-k">Published</span>
        <span class="adv-when-v" title="${esc(stampTs(a.published))}">${esc(dayTs(a.published))}</span>
        <span class="adv-when-k">Updated</span>
        <span class="adv-when-v${a.revised ? ' upd' : ' none'}"
              title="${a.revised ? esc(stampTs(a.updated)) : 'Not revised since publication'}">${
          a.revised ? esc(dayTs(a.updated)) : '—'}</span>
      </span>
      <svg class="adv-row-go" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
           stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
        <polyline points="9 6 15 12 9 18"/></svg>
    </button>`;
  }

  function empty() {
    const filtered = state.severity || state.product || state.window || state.q.trim();
    if (filtered) {
      return `<div class="adv-empty">
        <p class="adv-empty-t">No advisories match these filters.</p>
        <button class="adv-btn" id="advClear" type="button">Clear filters</button></div>`;
    }
    return `<div class="adv-empty">
      <p class="adv-empty-t">No advisories collected yet.</p>
      <p class="adv-empty-s">Advisories are collected from the vendor's published feed. Once the
      collector has run, everything it found appears here.</p></div>`;
  }

  function paintList() {
    const el = byId('advList'); if (!el) return;
    el.setAttribute('aria-busy', String(state.listLoading));

    const term = state.q.trim();
    if (term && term.length < MIN_QUERY) {
      el.innerHTML = '<p class="adv-hint">Enter at least 3 characters to search.</p>';
      return paintPager();
    }
    if (state.listError) {
      el.innerHTML = `<div class="adv-empty"><p class="adv-empty-t" role="alert">${esc(state.listError)}</p>
        <button class="adv-btn" id="advRetry" type="button">Retry</button></div>`;
      byId('advRetry').onclick = () => loadList(state.offset);
      return paintPager();
    }
    if (state.listLoading && !state.items.length) {
      // Skeletons at the real row height, so the page holds its shape while it waits instead of
      // collapsing and then shoving the pager back down.
      el.innerHTML = Array.from({ length: 6 }, () => '<div class="adv-skel"></div>').join('');
      return paintPager();
    }
    if (!state.items.length) {
      el.innerHTML = empty();
      byId('advClear')?.addEventListener('click', () => {
        state.severity = state.product = state.window = state.q = '';
        syncUrl(false); paintHead(); paintFilters(); loadList(0);
      });
      return paintPager();
    }

    el.innerHTML = state.items.map(row).join('');
    el.querySelectorAll('[data-id]').forEach(b => { b.onclick = () => openDrawer(b.dataset.id); });
    paintPager();
  }

  function paintPager() {
    const el = byId('advPager'); if (!el) return;
    const shownTo = state.offset + state.items.length;
    if (!state.total) { el.innerHTML = ''; return; }
    el.innerHTML = `
      <button class="adv-btn" id="advPrev" type="button" ${state.listLoading || state.offset === 0 ? 'disabled' : ''}>Previous</button>
      <span class="adv-pager-n">${num(state.offset + 1)}–${num(shownTo)} of ${num(state.total)}</span>
      <button class="adv-btn" id="advNext" type="button" ${state.listLoading || shownTo >= state.total ? 'disabled' : ''}>Next</button>`;
    byId('advPrev').onclick = () => loadList(Math.max(0, state.offset - PAGE));
    byId('advNext').onclick = () => loadList(state.offset + PAGE);
  }

  // ── Drawer ───────────────────────────────────────────────────────────────

  const TABS = [
    { id: 'overview',  label: 'Overview'  },
    { id: 'versions',  label: 'Affected'  },
    { id: 'mitigation',label: 'Mitigation'},
  ];

  function openDrawer(id) {
    if (!id) return;
    lastFocus = document.activeElement;
    state.openId = id; state.tab = 'overview'; state.detail = null; state.detailError = '';
    syncUrl(true);
    paintDrawer();
    byId('advDrawer')?.focus();
    markOpenRow();
    loadDetail(id);
  }

  function closeDrawer() {
    if (!state.openId) return;
    state.openId = ''; state.detail = null; state.detailError = '';
    syncUrl(true);
    paintDrawer();
    markOpenRow();
    // Back to the row that opened it, so a keyboard operator is not dropped at the top of the list.
    if (lastFocus && document.contains(lastFocus)) lastFocus.focus();
    lastFocus = null;
  }

  function markOpenRow() {
    document.querySelectorAll('#advList .adv-row').forEach(r => {
      const on = !!state.openId && r.dataset.id === state.openId;
      r.classList.toggle('open', on);
      r.setAttribute('aria-expanded', String(on));
    });
  }

  function kv(k, v, mono) {
    return `<div class="adv-kv"><span class="adv-kv-k">${esc(k)}</span>
      <span class="adv-kv-v${mono ? ' mono' : ''}">${v}</span></div>`;
  }

  // The feed hands these over as plain text with newlines, sometimes several entries deep. Rendered
  // as paragraphs rather than one block, and escaped — it is vendor prose, not markup we own.
  function prose(value) {
    const parts = (Array.isArray(value) ? value : [value])
      .map(v => (v && typeof v === 'object') ? (v.value || '') : (v || ''))
      .filter(Boolean);
    if (!parts.length) return '<p class="adv-none">Not stated in the advisory.</p>';
    return parts.map(t => String(t).split(/\n{2,}/).map(p =>
      `<p class="adv-p">${esc(p.trim()).replace(/\n/g, '<br>')}</p>`).join('')).join('');
  }

  function drawerOverview(d) {
    const sev = sevClass(d.severity);
    const vec = d.cvss || {};
    const chips = Object.keys(CVSS_LABEL)
      .filter(k => vec[k])
      .map(k => `<div class="adv-vec"><span class="adv-vec-k">${esc(CVSS_LABEL[k])}</span>
                 <span class="adv-vec-v">${esc(String(vec[k]))}</span></div>`).join('');

    return `
      <div class="adv-kvs">
        ${kv('Severity', `<span class="adv-sev-tag sev-${sev}">${esc(SEV_LABEL[sev])}</span>`)}
        ${kv('Base score', esc(score1(d.base_score)))}
        ${kv('Threat score', esc(score1(d.threat_score)))}
        ${kv('Published', esc(stampTs(d.published)))}
        ${kv('Last revised', d.updated && d.updated !== d.published
             ? `${esc(stampTs(d.updated))} <span class="adv-dim">(${esc(relDate(d.updated))})</span>`
             : '<span class="adv-dim">Not revised since publication</span>')}
        ${Array.isArray(d.aliases) && d.aliases.length
          ? kv('Also known as', d.aliases.map(a => esc(a)).join(', '), true) : ''}
      </div>
      ${chips ? `<div class="adv-sec">CVSS vector</div><div class="adv-vecs">${chips}</div>` : ''}
      <div class="adv-sec">Description</div>${prose(d.problem)}
      ${d.url ? `<div class="adv-link"><a href="${esc(d.url)}" target="_blank" rel="noopener noreferrer">
        Read on security.paloaltonetworks.com
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/>
          <polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg></a></div>` : ''}`;
  }

  function drawerVersions(d) {
    const rows = Array.isArray(d.versions) ? d.versions : [];
    if (!rows.length) return '<p class="adv-none">No affected version ranges are listed.</p>';
    return `<table class="adv-vtable">
      <thead><tr><th>Product</th><th>Affected</th><th>Fixed in</th></tr></thead>
      <tbody>${rows.map(v => `<tr>
        <td>${esc(v.product || '—')}</td>
        <td class="mono">${esc(v.affected || '—')}</td>
        <td class="mono${v.fixed ? ' fixed' : ''}">${esc(v.fixed || 'No fix listed')}</td>
      </tr>`).join('')}</tbody></table>`;
  }

  function drawerMitigation(d) {
    return `<div class="adv-sec">Solution</div>${prose(d.solution)}
            <div class="adv-sec">Workaround</div>${prose(d.work_around)}`;
  }

  function paintDrawer() {
    const drawer = byId('advDrawer'), overlay = byId('advOverlay');
    if (!drawer) return;
    const open = !!state.openId;
    drawer.classList.toggle('open', open);
    overlay.classList.toggle('open', open);
    drawer.setAttribute('aria-hidden', String(!open));
    // Nothing inside a shut drawer may be reached by Tab; `visibility:hidden` handles the pointer
    // but the panel stays in the tab order without this.
    drawer.inert = !open;
    if (!open) return;

    const d = state.detail;
    byId('advDrawerGauge').innerHTML = d ? gauge(d.severity, d.base_score, true) : '';
    byId('advDrawerT').textContent = state.openId;
    byId('advDrawerS').textContent = d ? (d.title || '') : (state.detailLoading ? 'Loading…' : '');

    const tabs = byId('advTabs');
    tabs.innerHTML = d ? TABS.map(t =>
      `<button class="adv-tab${state.tab === t.id ? ' on' : ''}" type="button" role="tab"
               aria-selected="${state.tab === t.id}" data-tab="${t.id}">${esc(t.label)}</button>`).join('') : '';
    tabs.querySelectorAll('[data-tab]').forEach(b => { b.onclick = () => {
      state.tab = b.dataset.tab; paintDrawer();
    }; });

    const body = byId('advDrawerB');
    if (state.detailLoading) body.innerHTML = '<p class="adv-hint" role="status">Loading advisory…</p>';
    else if (state.detailError) body.innerHTML = `<p class="adv-hint" role="alert">${esc(state.detailError)}</p>`;
    else if (!d) body.innerHTML = '';
    else body.innerHTML = state.tab === 'versions' ? drawerVersions(d)
                        : state.tab === 'mitigation' ? drawerMitigation(d)
                        : drawerOverview(d);
    if (!state.detailLoading) body.scrollTop = 0;
  }

  // ── Loads ────────────────────────────────────────────────────────────────

  async function loadOverview() {
    if (state.overviewLoading) return;
    state.overviewLoading = true; state.overviewError = '';
    try {
      state.overview = await api('/api/advisory/overview');
    } catch (e) {
      state.overviewError = e.message; state.overview = null;
    } finally {
      state.overviewLoading = false;
      paintHead(); paintNotice();
    }
  }

  // The refresh control asks the APPLIANCE to go and collect, it does not just re-read what is
  // already stored. Re-reading was all it did until now, which made it a button that could never
  // change its own answer.
  //
  // The collection is fire-and-forget on the wire (collectord owes mgmtd nothing; it answers by
  // writing rows), so the result is waited for the only way there is: poll the overview until the
  // sync stamp moves. A full walk of the feed is ~13s against a dozen throttled page fetches, so
  // the deadline is generous and the failure mode is "stop waiting and show what is there" rather
  // than an error — the rows may well have landed even if the stamp did not.
  const REFRESH_POLL_MS = 2000;
  const REFRESH_DEADLINE_MS = 120000;

  async function refreshNow() {
    if (state.refreshing) return;

    const before = state.overview?.last_sync || '';
    state.refreshing = true;
    state.overviewError = '';
    paintHead(); paintNotice();

    try {
      await api('/api/advisory/refresh', 'POST');
    } catch (e) {
      state.refreshing = false;
      state.overviewError = e.message;
      paintHead(); paintNotice();
      return;
    }

    const deadline = Date.now() + REFRESH_DEADLINE_MS;
    while (Date.now() < deadline) {
      await new Promise(r => setTimeout(r, REFRESH_POLL_MS));
      try {
        const o = await api('/api/advisory/overview');
        state.overview = o;
        paintHead();
        if ((o.last_sync || '') !== before) break;
      } catch (_) {
        // A poll that fails is not the run failing — keep waiting for the deadline.
      }
    }

    state.refreshing = false;
    paintHead();
    loadList(state.offset);
  }

  function paintNotice() {
    const el = byId('advNotice'); if (!el) return;
    el.innerHTML = state.overviewError
      ? `<p class="adv-notice" role="alert">${esc(state.overviewError)}</p>` : '';
  }

  async function loadList(offset) {
    const mine = ++listSeq;
    state.offset = offset; state.listLoading = true; state.listError = '';
    paintList();
    const term = state.q.trim();
    const p = new URLSearchParams({ offset: String(offset), limit: String(PAGE), sort: state.sort });
    if (state.severity)           p.set('severity', state.severity);
    if (state.product)            p.set('product', state.product);
    if (state.window)             p.set('window', state.window);
    if (term.length >= MIN_QUERY) p.set('q', term);
    try {
      const d = await api('/api/advisory/list?' + p);
      if (mine !== listSeq) return;
      state.items = Array.isArray(d.items) ? d.items : [];
      state.total = Number(d.total) || 0;
      // The product list belongs to the whole corpus, not to the filtered page, so the server sends
      // it whole and only the <select> is touched when it moves.
      if (Array.isArray(d.products) && d.products.join('\u0000') !== state.products.join('\u0000')) {
        state.products = d.products; patchProducts();
      }
      // A filter change can leave the operator past the end of a now-shorter result.
      if (!state.items.length && offset > 0) { state.listLoading = false; return loadList(0); }
    } catch (e) {
      if (mine !== listSeq) return;
      state.listError = e.message; state.items = []; state.total = 0;
    }
    if (mine === listSeq) { state.listLoading = false; paintList(); }
  }

  async function loadDetail(id) {
    const mine = ++detailSeq;
    state.detailLoading = true; state.detailError = ''; state.detail = null;
    paintDrawer();
    try {
      const d = await api('/api/advisory/detail?id=' + encodeURIComponent(id));
      if (mine !== detailSeq) return;
      state.detail = d;
    } catch (e) {
      if (mine !== detailSeq) return;
      state.detailError = e.message;
    }
    if (mine === detailSeq) { state.detailLoading = false; paintDrawer(); }
  }

  // ── Boot ─────────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    readUrl();
    shell();
    paintHead(); paintFilters(); paintList(); paintDrawer();
    loadOverview();
    loadList(0);
    if (state.openId) loadDetail(state.openId);
  });

  document.addEventListener('keydown', e => {
    if (e.key === 'Escape' && state.openId) { e.preventDefault(); closeDrawer(); }
  });

  // Back/forward steps through opened advisories and filter changes rather than leaving the page.
  window.addEventListener('popstate', () => {
    const before = JSON.stringify([state.severity, state.product, state.q, state.window, state.sort]);
    const openBefore = state.openId;
    readUrl();
    if (JSON.stringify([state.severity, state.product, state.q, state.window, state.sort]) !== before) {
      paintHead(); paintFilters(); loadList(0);
    }
    if (state.openId !== openBefore) {
      paintDrawer();
      if (state.openId) loadDetail(state.openId); else state.detail = null;
    }
  });
})();
