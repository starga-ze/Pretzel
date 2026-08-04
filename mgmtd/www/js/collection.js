/* collection.js — Insight › API Collection
 *
 * What this page is for
 * ---------------------
 * Configuration ▸ API Connector is where an operator DECLARES that an endpoint should be polled.
 * Nothing there can say whether it worked. This page is the other half of that sentence: for every
 * declared stream, is the cycle turning, is it turning on time, and what is coming back.
 *
 * Why it is not one big table
 * ---------------------------
 * A hundred endpoints are a hundred different JSON schemas. There is no set of columns that means
 * anything across "list every address object" and "show system info" — put them in one table and the
 * table is empty in every cell but the two both happen to have. So the payload is never tabulated
 * across streams. What IS shared by every stream is the SHAPE OF THE CALL — when, how long, how big,
 * did it answer — and that is what the grid shows, one row per stream, uniform and comparable. The
 * payload is opened one sample at a time, in the drawer, where the page can adapt to whatever that
 * single API returns.
 *
 * The unit: a stream
 * ------------------
 * One connector's one endpoint, on its own interval. Its identity is (connector, endpoint), it
 * belongs to a device, and the device belongs to a site — which is what makes the site selector at
 * the top meaningful and matches how Infrastructure scopes itself.
 *
 * Declared vs observed
 * --------------------
 * GET /api/collection/overview joins the operator's declaration with what the collector actually
 * produced, and the difference between the two is the whole point. A stream is judged:
 *
 *   error     config says it cannot run (no credential bound, endpoint deleted, legacy shape).
 *             collectord refuses these at load, so they would otherwise sit at "never" forever.
 *   disabled  declared, deliberately switched off.
 *   never     declared and runnable, but nothing has come back yet — normal for the first minute
 *             after a commit, a problem after that.
 *   failing   the last poll came back not-ok. The error is on the row.
 *   stale     the last poll was fine, but it was too long ago for the interval — the cycle STOPPED.
 *             This is the state that a naive "last result: ok" display hides completely, and the one
 *             an operator most needs, because nothing about it looks wrong until you check the clock.
 *   live      turning, on time.
 *
 * "Stale" is deliberately generous — three intervals, floor 90 seconds — so that one skipped tick on
 * a busy appliance is not reported as an outage.
 *
 * Honesty about the payload
 * -------------------------
 * The Data tab derives its table from the response itself: it finds the record array, unions the
 * keys, and renders them. That is a GUESS about an unknown schema, and it is always one click from
 * Raw, which is the bytes the device sent, unmodified. Where the guess and the payload disagree, Raw
 * is the truth — which is why the raw response is stored and served verbatim rather than normalised
 * on the way in.
 *
 * Bodies are retained for the most recent samples per stream only (engined's CollectionService
 * prunes them); an older sample keeps its row and its timings but reports its payload as released
 * rather than pretending the poll returned nothing.
 */
(function () {
  'use strict';

  // Live re-runs exactly what the topbar's refresh button runs (NMS.onRefresh → load), on a timer.
  // One minute: collection intervals are declared in minutes and hours, so a faster poll would
  // redraw the same numbers, and the interval is stated on the button rather than left to be
  // guessed at.
  const REFRESH_MS = 60000;
  const REFRESH_LABEL = '1m';
  const SAMPLE_PAGE = 50;
  const MAX_TABLE_COLS = 14;
  const MAX_TABLE_ROWS = 300;    // beyond this the drawer is a scroll test, not a reading
  const MAX_TREE_NODES = 4000;

  // The site is a scope, not a filter: it travels to mgmtd, which enumerates only that site's
  // streams. So switching sites is a re-fetch, and the page has a real waiting state — held for a
  // minimum so it is legible, since the read itself settles far faster than the eye does.
  const MIN_FETCH_MS = 2000;
  // What is actually happening, in order. mgmtd reads the database directly here — there is no
  // second daemon in the path and nothing is composed, so these say "fetch", not "render".
  const FETCH_STAGES = [
    'Requesting collection state',
    'Reading collection samples',
    'Measuring stream health',
    'Fetching records\u2026',
  ];
  const RING_R = 52;
  const RING_C = 2 * Math.PI * RING_R;

  const esc = (s) => window.NMS.utils.esc(s);
  const parseTs = (s) => window.NMS.utils.parseTs(s);
  const relAge = (s) => window.NMS.utils.relAge(s);

  const state = {
    site: '',            // '' = Overview; otherwise the scope being listed
    booting: true,       // first load after a restored scope
    fetching: false,     // the ring owns the page until it finishes
    answered: false,     // the response is in; the ring may complete
    health: 'all',       // chip filter
    q: '',
    live: true,
    streams: [],
    sites: [],
    orphans: 0,
    windowHours: 24,
    error: '',
    closed: {},          // group key → collapsed
    // Drawer
    sel: null,           // selected stream key
    tab: 'overview',
    sample: null,        // { meta, body } of the sample whose payload is on screen
    sampleOid: '',
    sampleErr: '',
    sampleLoading: false,
    rawPretty: true,     // Raw tab: reflowed for reading, or the exact stored bytes
    list: { rows: [], cursor: null, status: '', loading: false },
  };

  // Come back where you left off. Restoring is NOT a scope change: no ring, because the operator did
  // not ask for anything — they returned to a page they were already on.
  state.site = window.NMS.utils.siteScope.get();

  let timer = null;
  let ringTimer = null;
  let fetchHold = null;
  let fetchStart = 0;

  // ── Stream judgement ────────────────────────────────────────────────────────
  // One stream's interval decides how late is late. Three ticks, never less than 90 seconds: a
  // single skipped poll is noise, two in a row is a stopped cycle.
  const staleAfterSec = (intervalSec) => Math.max(3 * (Number(intervalSec) || 60), 90);

  function ageSec(ts) {
    const t = parseTs(ts);
    return t ? Math.max(0, (Date.now() - t.getTime()) / 1000) : null;
  }

  function healthOf(s) {
    if (s.config_error) return 'error';
    if (s.enabled === false) return 'disabled';
    if (!s.last) return 'never';
    if (!s.last.ok) return 'failing';
    const age = ageSec(s.last.at);
    return (age !== null && age > staleAfterSec(s.interval_sec)) ? 'stale' : 'live';
  }

  const HEALTH_LABEL = {
    live: 'live', failing: 'failing', stale: 'stale',
    never: 'no data', disabled: 'disabled', error: 'config error',
  };

  // Observed cadence: the median gap between consecutive samples in the spark series. Compared
  // against the declared interval, this is what turns "it ran" into "it ran on schedule".
  function observedIntervalSec(s) {
    const pts = (s.spark || []).map(p => parseTs(p.at)).filter(Boolean);
    if (pts.length < 3) return null;
    const gaps = [];
    for (let i = 1; i < pts.length; i++) gaps.push((pts[i].getTime() - pts[i - 1].getTime()) / 1000);
    gaps.sort((a, b) => a - b);
    return gaps[Math.floor(gaps.length / 2)];
  }

  // ── Scope ───────────────────────────────────────────────────────────────────
  // Two levels, deliberately. `scoped` is what the operator is looking at — a site, maybe a search;
  // the summary tiles and the chip counts are measured over it. `visible` adds the health chip on
  // top, and only the grid uses it: a count that changed when you clicked it would be no use for
  // deciding what to click next.
  function scoped() {
    const q = state.q.trim().toLowerCase();
    return state.streams.filter(s => {
      if (state.site && (s.site_oid || '') !== state.site) return false;
      if (!q) return true;
      const hay = [s.connector_name, s.endpoint_name, s.endpoint_path, s.device_name,
                   s.device_target, s.site_name].join(' ').toLowerCase();
      return hay.indexOf(q) !== -1;
    });
  }

  // A config error is a failure the operator has to act on, so it rides with `failing` rather than
  // taking a chip of its own.
  const bucketOf = (s) => { const h = healthOf(s); return h === 'error' ? 'failing' : h; };

  function visible() {
    const rows = scoped();
    return state.health === 'all' ? rows : rows.filter(s => bucketOf(s) === state.health);
  }

  const byKey = (key) => state.streams.find(s => s.key === key) || null;

  // Every declared site, plus any site a stream's device names that config did not list. A site with
  // no streams stays selectable — an empty grid under it is an answer, not a missing option.
  function siteOptions() {
    const seen = new Map(state.sites.map(s => [s.oid, s.name || s.oid]));
    state.streams.forEach(s => {
      if (s.site_oid && !seen.has(s.site_oid)) seen.set(s.site_oid, s.site_name || s.site_oid);
    });
    return [...seen.entries()].sort((a, b) => String(a[1]).localeCompare(String(b[1])));
  }

  // ── Waiting ─────────────────────────────────────────────────────────────────
  // The same indicator Insight ▸ Infrastructure uses. The two pages fetch differently — that one
  // asks a daemon to compose, this one reads the database — but from the operator's side both are
  // "the scope changed, the page is getting it", and one waiting state for both is one thing to
  // learn rather than two.
  function ringHtml() {
    return `<div class="col-panel col-fetching">
        <div class="col-ring-wrap">
          <svg class="col-ring" viewBox="0 0 120 120" aria-hidden="true">
            <circle class="cr-track" cx="60" cy="60" r="${RING_R}"/>
            <circle class="cr-fill" cx="60" cy="60" r="${RING_R}"
                    stroke-dasharray="${RING_C.toFixed(1)}" stroke-dashoffset="${RING_C.toFixed(1)}"/>
          </svg>
          <div class="col-ring-pct">0<small>%</small></div>
        </div>
        <div class="col-fetch-t">Fetching this site&rsquo;s collection state</div>
        <div class="col-fetch-stage">${esc(FETCH_STAGES[0])}</div>
      </div>`;
  }

  function tickRing() {
    const fill = document.querySelector('.col-ring .cr-fill');
    if (!fill) return;

    // 100% only once the answer is in hand: a full ring over an unfinished fetch turns "working"
    // into "done, and nothing happened".
    const elapsed = Date.now() - fetchStart;
    const pct = Math.min(state.answered ? 1 : 0.9, elapsed / MIN_FETCH_MS);
    fill.setAttribute('stroke-dashoffset', (RING_C * (1 - pct)).toFixed(1));

    const p = document.querySelector('.col-ring-pct');
    if (p) p.innerHTML = Math.round(pct * 100) + '<small>%</small>';

    const s = document.querySelector('.col-fetch-stage');
    if (s) {
      const label = (!state.answered && elapsed > MIN_FETCH_MS)
        ? 'Still waiting\u2026'
        : FETCH_STAGES[Math.min(FETCH_STAGES.length - 1, Math.floor(pct * FETCH_STAGES.length))];
      if (s.textContent !== label) s.textContent = label;
    }
  }

  function startFetching() {
    if (state.fetching) return;
    state.fetching = true;
    state.answered = false;
    fetchStart = Date.now();
    clearInterval(ringTimer);
    ringTimer = setInterval(tickRing, 40);
  }

  function finishFetching(then) {
    if (!state.fetching) return then();
    clearTimeout(fetchHold);
    fetchHold = setTimeout(() => {
      clearInterval(ringTimer);
      ringTimer = null;
      state.fetching = false;
      then();
    }, Math.max(0, MIN_FETCH_MS - (Date.now() - fetchStart)));
  }

  // ── Load ────────────────────────────────────────────────────────────────────
  async function load() {
    try {
      // The scope travels with the request; with none chosen mgmtd answers with the site list alone,
      // which is exactly what the page needs to ask the question.
      const d = await window.NMS.utils.fetchJSON(
        '/api/collection/overview?window=' + state.windowHours + '&site=' + encodeURIComponent(state.site));
      if (!d) return;
      state.streams = Array.isArray(d.streams) ? d.streams : [];
      state.sites = Array.isArray(d.sites) ? d.sites : [];
      state.orphans = d.orphan_streams || 0;
      state.windowHours = d.window_hours || state.windowHours;
      state.error = '';

      // A remembered site can have been deleted between visits, and a first visit to a one-site
      // estate has exactly one sensible answer. Both settled here, on the first answer only.
      if (state.booting) {
        state.booting = false;
        const known = (state.sites || []).map(x => x.oid);
        if (state.site && known.indexOf(state.site) === -1) {
          state.site = '';
          window.NMS.utils.siteScope.set('');
          return load();
        }
        if (!state.site && known.length === 1 && !window.NMS.utils.siteScope.get()) {
          state.site = known[0];
          window.NMS.utils.siteScope.set(state.site);
          return load();
        }
      }
    } catch (e) {
      state.error = 'refresh failed';
      state.booting = false;
    }

    state.answered = true;
    // A ring that is up runs out its length before the page appears; everything else paints now.
    if (state.fetching) finishFetching(paint);
    else paint();
  }

  // ── Shell ───────────────────────────────────────────────────────────────────
  // Built once. Refreshes repaint the tiles, the grid and the stamps — never the whole page, so the
  // search box keeps focus and the drawer keeps its scroll position through a poll.
  function mount() {
    const root = document.getElementById('contentBody');
    if (!root) return;
    root.className = 'content-body col-page';
    root.innerHTML =
      `<div class="col-bar">
         <div class="col-bar-group">
           <span class="col-bar-label">Site</span>
           <select class="col-select" id="colSite"></select>
         </div>
         <div class="col-chips" id="colChips"></div>
         <span class="col-bar-spacer"></span>
         <input class="col-search" id="colQ" type="search" placeholder="Search stream, device, path…" />
         <button class="col-toggle" id="colLive" type="button"
                 title="Refreshes this page every ${REFRESH_LABEL} — the same refresh as the button in the title bar">
           <span class="col-live-dot"></span>Live · ${REFRESH_LABEL}</button>
         <span class="col-stamp" id="colStamp"></span>
       </div>
       <div class="col-tiles" id="colTiles"></div>
       <div class="col-groups" id="colGroups"></div>
       <div class="col-note" id="colNote"></div>
       <div class="col-drawer-overlay" id="colOverlay"></div>
       <aside class="col-drawer" id="colDrawer">
         <div class="col-drawer-h">
           <div style="min-width:0">
             <div class="col-drawer-t" id="colDrawerT">Stream</div>
             <div class="col-drawer-sub" id="colDrawerS"></div>
           </div>
           <button class="col-drawer-x" id="colDrawerX" type="button" aria-label="Close">&times;</button>
         </div>
         <div class="col-tabs" id="colTabs">
           <button class="col-tab" data-t="overview" type="button">Overview</button>
           <button class="col-tab" data-t="samples" type="button">Samples</button>
           <button class="col-tab" data-t="data" type="button">Data</button>
           <button class="col-tab" data-t="raw" type="button">Raw</button>
         </div>
         <div class="col-drawer-b" id="colDrawerB"></div>
       </aside>`;

    wireShell();
    paint();
  }

  function wireShell() {
    const sel = document.getElementById('colSite');
    // Guarded: paintBar re-fires 'change' after rebuilding the option list, purely to make the themed
    // dropdown resync its label. Only a real selection change is worth a repaint.
    sel.addEventListener('change', (e) => {
      if (e.target.value === state.site) return;
      state.site = e.target.value;
      window.NMS.utils.siteScope.set(state.site);
      // The streams on screen belong to the site being left. Clearing them first is what makes the
      // ring appear — otherwise they count as data already in hand.
      state.streams = [];
      state.sel = null;
      if (state.site) startFetching();
      paint();
      load();
    });

    document.getElementById('colChips').addEventListener('click', (e) => {
      const b = e.target.closest('button[data-h]');
      if (!b) return;
      state.health = b.dataset.h;
      paint();
    });

    // The search narrows what the tiles and the chip counts are measured over too, so all three
    // repaint. The drawer is left alone — it is a reading of one stream and does not belong to the
    // search. Rebuilding the bar does not touch the input element itself, so focus survives.
    let debounce = null;
    document.getElementById('colQ').addEventListener('input', (e) => {
      clearTimeout(debounce);
      const v = e.target.value;
      debounce = setTimeout(() => {
        state.q = v;
        paintBar();
        paintTiles();
        paintGroups();
      }, 160);
    });

    document.getElementById('colLive').addEventListener('click', () => {
      state.live = !state.live;
      schedule();
      paintBar();
    });

    document.getElementById('colGroups').addEventListener('click', (e) => {
      const card = e.target.closest('[data-site]');
      if (card) {
        const sel = document.getElementById('colSite');
        if (sel) { sel.value = card.dataset.site; sel.dispatchEvent(new Event('change', { bubbles: true })); }
        return;
      }

      const head = e.target.closest('.col-group-h');
      if (head) {
        const key = head.parentElement.dataset.key;
        state.closed[key] = !state.closed[key];
        paintGroups();
        return;
      }
      const row = e.target.closest('.col-row');
      if (row) openStream(row.dataset.key);
    });

    document.getElementById('colDrawerX').addEventListener('click', closeDrawer);
    document.getElementById('colOverlay').addEventListener('click', closeDrawer);
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape') closeDrawer(); });

    document.getElementById('colTabs').addEventListener('click', (e) => {
      const b = e.target.closest('button[data-t]');
      if (!b) return;
      state.tab = b.dataset.t;
      paintDrawer();
      if (state.tab === 'samples' && !state.list.rows.length) loadSamples(true);
    });

    document.getElementById('colDrawerB').addEventListener('click', onDrawerClick);
  }

  // ── Paint ───────────────────────────────────────────────────────────────────
  // The whole page from current state. Called on load and on each poll, so the drawer is repainted
  // in `live` mode — see paintDrawer.
  function paint() {
    paintBar();
    paintTiles();
    paintGroups();
    paintDrawer(true);
  }

  // Both Insight pages are per-site now. Here the scope decides which streams are enumerated at all
  // — mgmtd returns one site's, not the estate's — so with none chosen there is nothing to draw and
  // the page says so rather than showing an empty grid that looks like an empty estate.
  const scopeChosen = () => !!state.site;

  function paintBar() {
    const sel = document.getElementById('colSite');
    if (sel) {
      const opts = siteOptions();
      const want = `<option value="">Overview</option>` + opts.map(([oid, name]) =>
        `<option value="${esc(oid)}">${esc(name)}</option>`).join('');
      // The themed dropdown reads select.options fresh each time it opens, so replacing them is
      // safe; only its collapsed label is cached, and a change event is what resyncs it. Guarded by
      // a signature because the site list moves on a config commit, not on every poll.
      if (sel.dataset.sig !== want) {
        sel.dataset.sig = want;
        sel.innerHTML = want;
        sel.value = state.site;
        window.NMS.utils.enhanceSelect?.(sel);   // no-op once enhanced
        sel.dispatchEvent(new Event('change'));
      } else if (sel.value !== state.site) {
        sel.value = state.site;
      }
    }

    const rows = scoped();
    const n = { all: rows.length, live: 0, failing: 0, stale: 0, never: 0 };
    rows.forEach(s => { const b = bucketOf(s); if (n[b] !== undefined) n[b]++; });

    const chips = [['all', 'All'], ['live', 'Live'], ['failing', 'Failing'], ['stale', 'Stale'], ['never', 'No data']];
    const chipsEl = document.getElementById('colChips');
    if (chipsEl) {
      chipsEl.innerHTML = chips.map(([k, label]) =>
        `<button class="col-chip ${state.health === k ? 'active' : ''}" data-h="${k}" type="button">${label}
           <span class="col-chip-n">${n[k] || 0}</span></button>`).join('');
    }

    document.getElementById('colLive')?.classList.toggle('on', state.live);

    const stamp = document.getElementById('colStamp');
    if (stamp) {
      // The same stamp Insight ▸ Infrastructure shows, measuring the same thing: when the DATA was
      // last collected from a device. It used to report when the browser last fetched, which reads
      // "just now" forever — true, useless, and indistinguishable from a healthy estate on a dead
      // one. The newest sample across the scope is the honest summary; each row states its own age.
      let newest = 0;
      rows.forEach(s => {
        const t = s.last && parseTs(s.last.at);
        if (t && t.getTime() > newest) newest = t.getTime();
      });

      stamp.innerHTML = state.error
        ? `<b style="color:var(--red)">refresh failed</b> — showing last known`
        : `<span title="When this scope's data was last collected from a device. The page itself refreshes every ${REFRESH_LABEL}.">polled <b>${
            newest ? esc(relAge(new Date(newest).toISOString())) : 'never'}</b></span>`;
    }
  }

  function paintTiles() {
    const el = document.getElementById('colTiles');
    if (!el) return;

    // Nothing to summarise before a scope is chosen, and nothing worth summarising while it is being
    // fetched — a row of dashes under a spinning ring is noise.
    if (!scopeChosen() || state.fetching) { el.innerHTML = ''; return; }

    const rows = scoped();

    let live = 0, attention = 0, total = 0, ok = 0, latencies = [];
    rows.forEach(s => {
      const h = healthOf(s);
      if (h === 'live') live++;
      if (h === 'failing' || h === 'stale' || h === 'error') attention++;
      const w = s.window || {};
      total += Number(w.total) || 0;
      ok += Number(w.ok) || 0;
      if (w.p50_latency_ms != null) latencies.push(Number(w.p50_latency_ms));
    });

    latencies.sort((a, b) => a - b);
    const p50 = latencies.length ? latencies[Math.floor(latencies.length / 2)] : null;
    const rate = total ? (ok / total) * 100 : null;

    const tile = (k, v, sub, cls) =>
      `<div class="col-tile ${cls || ''}"><span class="col-tile-k">${k}</span>
         <span class="col-tile-v">${v}</span><span class="col-tile-sub">${sub}</span></div>`;

    el.innerHTML =
      tile('Collecting', `${live}<small>/ ${rows.length}</small>`,
           rows.length ? 'streams turning on schedule' : 'no streams in scope') +
      tile('Needs attention', String(attention),
           'failing, stalled or misconfigured', attention ? 'is-alert' : '') +
      tile(`Success · ${state.windowHours}h`,
           rate === null ? '—' : `${rate.toFixed(1)}<small>%</small>`,
           `${total.toLocaleString()} sample${total === 1 ? '' : 's'} collected`,
           rate !== null && rate < 95 ? 'is-warn' : '') +
      tile('Median latency', p50 === null ? '—' : `${p50}<small>ms</small>`,
           'across streams in scope');
  }

  // Streams are grouped by the device they are collected from: a connector belongs to exactly one
  // device, so this is the estate's own hierarchy (site → device → its APIs) rather than a view
  // choice, and "is this box answering" is the question the grid is scanned for.
  function groupsOf(rows) {
    const map = new Map();
    rows.forEach(s => {
      const key = s.device_oid || 'unassigned';
      let g = map.get(key);
      if (!g) {
        g = {
          key,
          streams: [],
          title: s.device_name || s.device_target || 'Unknown device',
          sub: [s.site_name || 'no site', s.device_target].filter(Boolean).join(' · '),
          kind: s.device_type || '',
        };
        map.set(key, g);
      }
      g.streams.push(s);
    });

    const out = [...map.values()];
    // Groups holding something that needs attention float up; the rest are alphabetical.
    const worst = (g) => g.streams.some(s => ['failing', 'error'].includes(healthOf(s))) ? 0
                       : g.streams.some(s => healthOf(s) === 'stale') ? 1
                       : g.streams.some(s => healthOf(s) === 'never') ? 2 : 3;
    out.sort((a, b) => worst(a) - worst(b) || a.title.localeCompare(b.title));
    out.forEach(g => g.streams.sort((a, b) =>
      (a.endpoint_name || '').localeCompare(b.endpoint_name || '')));
    return out;
  }

  function paintGroups() {
    const el = document.getElementById('colGroups');
    const note = document.getElementById('colNote');
    if (!el) return;

    if (note) note.innerHTML = '';

    if (state.fetching) { el.innerHTML = ringHtml(); return; }
    if (state.booting && state.site) { el.innerHTML = ''; return; }

    if (!scopeChosen()) {
      // The estate at rest, not a blocked page. Rows rather than a card grid: one card in a grid
      // reads as a layout that failed, one row reads as a list with one thing in it, and rows keep
      // working at forty sites.
      const sites = (state.sites || []).slice()
        .sort((a, b) => String(a.name || '').localeCompare(String(b.name || '')));

      if (!sites.length) {
        el.innerHTML = `<div class="col-panel">
          <div class="col-panel-t">No site configured</div>
          <div class="col-panel-s">Add one in <a href="settings?tab=sites">Configuration ▸ Sites</a>.</div>
        </div>`;
        return;
      }

      const tile = (v, label) => `<div class="col-ov-tile"><b>${v}</b><span>${esc(label)}</span></div>`;
      // `sub` carries the breakdown under the headline number, so "2 devices" can say what those two
      // are without becoming two more columns.
      const stat = (v, label, sub) => `<span class="col-ov-stat"><b>${v}</b>${esc(label)}${
        sub ? `<i>${esc(sub)}</i>` : ''}</span>`;

      const rows = sites.map(x => {
        const ngfw = x.ngfw || 0;
        const sase = x.sase || 0;
        return `<button class="col-ov-row" type="button" data-site="${esc(x.oid)}">
          <span class="col-ov-nm">${esc(x.name || x.oid)}</span>
          <span class="col-ov-stats">
            ${stat(ngfw + sase, (ngfw + sase) === 1 ? 'device' : 'devices', `${sase} SASE · ${ngfw} NGFW`)}
            ${stat(x.endpoints || 0, (x.endpoints === 1 ? 'API endpoint' : 'API endpoints'))}
          </span>
          <span class="col-ov-go">&rsaquo;</span>
        </button>`;
      }).join('');

      el.innerHTML = `<div class="col-panel col-overview">
          <div class="col-ov-h">Overview</div>
          <div class="col-ov-tiles">
            ${tile(sites.length, sites.length === 1 ? 'site' : 'sites')}
          </div>
          <div class="col-ov-list">${rows}</div>
          <div class="col-ov-note">Open a site to see what each of its APIs is returning.</div>
        </div>`;
      return;
    }

    const rows = visible();
    if (!rows.length) {
      el.innerHTML = emptyHtml();
      if (note) note.innerHTML = '';
      return;
    }

    el.innerHTML = groupsOf(rows).map(g => {
      const closed = !!state.closed[g.key];
      const bad = g.streams.filter(s => ['failing', 'error'].includes(healthOf(s))).length;
      const stale = g.streams.filter(s => healthOf(s) === 'stale').length;
      return `<section class="col-group ${closed ? 'is-closed' : ''}" data-key="${esc(g.key)}">
          <div class="col-group-h">
            <svg class="col-group-caret" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                 stroke-width="2.4"><polyline points="6 9 12 15 18 9"/></svg>
            <div class="col-group-meta">
              <span class="col-group-nm">${esc(g.title)}</span>
              <span class="col-group-sub">${esc(g.sub)}</span>
            </div>
            <div class="col-group-tags">
              ${g.kind ? `<span class="col-kind ${esc(g.kind)}">${esc(g.kind)}</span>` : ''}
              ${bad ? `<span class="col-state failing">${bad} failing</span>` : ''}
              ${stale ? `<span class="col-state stale">${stale} stalled</span>` : ''}
              <span class="col-group-sub">${g.streams.length} stream${g.streams.length === 1 ? '' : 's'}</span>
            </div>
          </div>
          <div class="col-group-body">${headHtml()}${g.streams.map(rowHtml).join('')}</div>
        </section>`;
    }).join('');

    if (note) {
      note.innerHTML = state.orphans
        ? `${state.orphans} sample stream${state.orphans === 1 ? '' : 's'} in the database no longer
           match a connector in the configuration — retained until the retention window drops them.`
        : '';
    }
  }

  function emptyHtml() {
    if (!state.streams.length) {
      return `<div class="col-empty"><b>Nothing is being collected yet</b>
        An API Connector binds a device, a credential and the endpoints to poll. Define one in
        <a href="settings?tab=api-connector">Configuration ▸ API Connector</a> and its streams appear
        here on the first cycle.</div>`;
    }
    return `<div class="col-empty"><b>No stream matches this view</b>
      ${state.q ? 'Try a different search term, or ' : ''}widen the site or status filter.</div>`;
  }

  // Column header, on the same grid as the rows. The spark in particular is not self-explanatory —
  // it needs to be named before it can be read as a series of polls rather than as decoration.
  function headHtml() {
    return `<div class="col-head">
        <span></span>
        <span>Stream</span>
        <span class="col-hide-md">Endpoint</span>
        <span class="col-hide-sm">Recent polls →</span>
        <span style="text-align:right">Last collected</span>
        <span class="col-hide-md" style="text-align:right">Schedule</span>
        <span class="col-cell-status">Status</span>
      </div>`;
  }

  function rowHtml(s) {
    const h = healthOf(s);
    const last = s.last;
    const w = s.window || {};
    const okN = Number(w.ok) || 0;
    const totalN = Number(w.total) || 0;

    // Spans, not divs: the whole row is a <button>, and flow content inside phrasing content inside
    // a button is where browsers start reparenting nodes.
    const lastCell = last
      ? `<span class="col-num">${esc(relAge(last.at))}</span>
         <span class="col-sub" style="text-align:right">${
           last.ok ? (last.latency_ms != null ? last.latency_ms + ' ms' : 'ok')
                   : esc('HTTP ' + (last.http_status != null ? last.http_status : '—'))}</span>`
      : `<span class="col-num muted">never</span>`;

    return `<button class="col-row ${state.sel === s.key ? 'is-selected' : ''}" type="button"
              data-key="${esc(s.key)}">
        <span class="col-dot ${h}" title="${esc(HEALTH_LABEL[h])}"></span>
        <span class="col-cell">
          <span class="col-nm">${esc(s.endpoint_name || 'Unnamed endpoint')}</span>
          <span class="col-sub">${esc(s.connector_name || 'connector')}${
            s.site_name ? ' · ' + esc(s.site_name) : ''}</span>
        </span>
        <span class="col-cell col-hide-md">
          <span class="col-path" title="${esc(s.endpoint_path)}">${esc(s.endpoint_path || '—')}</span>
          <span class="col-sub">${esc((s.api_type || '').toUpperCase())}${
            totalN ? ' · ' + okN + '/' + totalN + ' ok' : ''}</span>
        </span>
        <span class="col-cell col-hide-sm">${sparkHtml(s)}</span>
        <span class="col-cell">${lastCell}</span>
        <span class="col-cell col-hide-md">
          <span class="col-num muted">${esc(intervalLabel(s))}</span>
        </span>
        <span class="col-cell col-cell-status"><span class="col-state ${h}">${esc(HEALTH_LABEL[h])}</span></span>
      </button>`;
  }

  function intervalLabel(s) {
    const iv = Number(s.interval_sec) || 0;
    const txt = iv >= 3600 ? Math.round(iv / 3600) + 'h' : iv >= 60 ? Math.round(iv / 60) + 'm' : iv + 's';
    return 'every ' + txt;
  }

  // One bar per poll: height is latency, colour is outcome. A failed poll has no latency worth
  // plotting, so it is drawn full height in red — a break in the rhythm should be the loudest thing
  // in the row, not the shortest bar.
  //
  // Bars keep a fixed narrow width instead of dividing the track between them, and the series is
  // anchored to the RIGHT edge. Dividing would make three samples read as three fat blocks and
  // thirty as hairlines — the same rhythm drawn two unrelated ways — and floating the last bar
  // wherever the count happened to end would put "now" in a different place on every row. Anchored
  // right, the newest poll is always against the Last-collected column, and a short history simply
  // leaves the track empty behind it, which is the honest picture.
  const BAR_W = 4, BAR_GAP = 2;

  function sparkHtml(s) {
    const pts = s.spark || [];
    if (!pts.length) return `<span class="col-spark-empty">no polls yet</span>`;

    const W = 116, H = 26;
    const step = BAR_W + BAR_GAP;
    const fit = Math.max(1, Math.floor((W + BAR_GAP) / step));
    const shown = pts.slice(-fit);                       // newest survive a long history
    const x0 = W - (shown.length * step - BAR_GAP);      // right-anchored
    const max = Math.max(1, ...shown.map(p => Number(p.latency_ms) || 0));

    const bars = shown.map((p, i) => {
      const bad = !p.ok;
      const v = Number(p.latency_ms) || 0;
      const hgt = bad ? H : Math.max(2, (v / max) * H);
      const title = `${p.at ? relAge(p.at) : ''} — ${bad ? 'failed' : v + ' ms'}`;
      return `<rect class="${bad ? 'bad' : ''}" x="${(x0 + i * step).toFixed(1)}"
               y="${(H - hgt).toFixed(1)}" width="${BAR_W}" height="${hgt.toFixed(1)}" rx="1.2"
               ><title>${esc(title)}</title></rect>`;
    }).join('');

    const title = `${pts.length} poll${pts.length === 1 ? '' : 's'} in the last ${state.windowHours}h`
      + ` · tallest bar = ${max} ms`;
    return `<svg class="col-spark" viewBox="0 0 ${W} ${H}"><title>${esc(title)}</title>${bars}</svg>`;
  }

  // ── Drawer ──────────────────────────────────────────────────────────────────
  function openStream(key) {
    const s = byKey(key);
    if (!s) return;
    state.sel = key;
    state.tab = 'overview';
    state.list = { rows: [], cursor: null, status: '', loading: false };
    state.sample = null;
    state.sampleErr = '';
    state.sampleOid = s.last ? String(s.last.oid) : '';
    paintGroups();
    paintDrawer();
    if (state.sampleOid) loadSample(state.sampleOid);
  }

  function closeDrawer() {
    if (!state.sel) return;
    state.sel = null;
    paintGroups();
    paintDrawer();
  }

  // `live` marks the call as coming from the background poll rather than from something the operator
  // did. A poll may refresh Overview — that tab IS live data — but must not touch Samples, Data or
  // Raw: those are a reading of one fixed sample, and rebuilding them every 15 seconds would collapse
  // an opened JSON tree and throw away the scroll position mid-sentence.
  function paintDrawer(live) {
    const drawer = document.getElementById('colDrawer');
    const overlay = document.getElementById('colOverlay');
    const s = state.sel ? byKey(state.sel) : null;

    drawer?.classList.toggle('open', !!s);
    overlay?.classList.toggle('open', !!s);
    if (!s) return;

    document.getElementById('colDrawerT').textContent = s.endpoint_name || 'Unnamed endpoint';
    document.getElementById('colDrawerS').textContent =
      [s.device_name || s.device_target, s.site_name, s.connector_name].filter(Boolean).join(' · ');

    document.querySelectorAll('#colTabs .col-tab').forEach(b =>
      b.classList.toggle('on', b.dataset.t === state.tab));

    if (live && state.tab !== 'overview') return;

    const body = document.getElementById('colDrawerB');
    if (state.tab === 'overview') body.innerHTML = overviewHtml(s);
    else if (state.tab === 'samples') body.innerHTML = samplesHtml(s);
    else if (state.tab === 'data') body.innerHTML = dataHtml(s);
    else body.innerHTML = rawHtml(s);
  }

  function overviewHtml(s) {
    const h = healthOf(s);
    const w = s.window || {};
    const total = Number(w.total) || 0;
    const ok = Number(w.ok) || 0;
    const rate = total ? (ok / total) * 100 : null;
    const obs = observedIntervalSec(s);
    const declared = Number(s.interval_sec) || 0;
    // A quarter off the declared interval is drift worth naming; anything less is scheduler jitter.
    const drift = (obs && declared) ? Math.abs(obs - declared) / declared : 0;

    let banner = '';
    if (s.config_error) {
      banner = `<div class="col-banner bad"><b>This stream cannot run</b>${esc(s.config_error)}.
        Fix it in <a href="settings?tab=api-connector">Configuration ▸ API Connector</a>.</div>`;
    } else if (s.enabled === false) {
      banner = `<div class="col-banner"><b>Collection is switched off</b>
        The item is declared but disabled, so no polls are scheduled for it.</div>`;
    } else if (h === 'failing') {
      banner = `<div class="col-banner bad"><b>Last poll failed</b>${
        esc(s.last.error || ('HTTP ' + (s.last.http_status != null ? s.last.http_status : '—')))}</div>`;
    } else if (h === 'stale') {
      banner = `<div class="col-banner warn"><b>The cycle has stopped</b>
        The last poll succeeded ${esc(relAge(s.last.at))}, but this stream is declared to run
        ${esc(intervalLabel(s))}. The collector is not producing samples for it — check that
        collectord is running and that the connector survived the last commit.</div>`;
    } else if (h === 'never') {
      banner = `<div class="col-banner warn"><b>No sample yet</b>
        The stream is declared and runnable but nothing has come back. The first poll follows shortly
        after a commit; if this persists past a few intervals, check collectord's log.</div>`;
    }

    const mini = (k, v, sub) =>
      `<div class="col-mini-c"><div class="col-mini-k">${k}</div>
        <div class="col-mini-v">${v}</div>${sub ? `<div class="col-tile-sub">${sub}</div>` : ''}</div>`;

    const last = s.last;
    return banner +
      `<div class="col-sec">Cadence</div>
       <dl class="col-kv">
         <dt>Declared</dt><dd>${esc(intervalLabel(s))}${s.enabled === false ? ' · disabled' : ''}</dd>
         <dt>Observed</dt><dd>${obs === null
           ? '<span class="col-null">not enough samples to measure</span>'
           : `every ${Math.round(obs)}s${drift > 0.25
               ? ` <span class="col-state stale">— drifting from the declared ${declared}s</span>`
               : ' <span class="col-state live">— on schedule</span>'}`}</dd>
         <dt>Last collected</dt><dd>${last
           ? `${esc(relAge(last.at))} <span class="col-tile-sub">${esc(fmtTs(last.at))}</span>`
           : '<span class="col-null">never</span>'}</dd>
       </dl>

       <div class="col-sec">Last ${state.windowHours} hours</div>
       ${total ? `<div class="col-ratio" title="${ok} of ${total} succeeded">
           <span style="width:${rate.toFixed(1)}%"></span></div>` : ''}
       <div class="col-mini" style="margin-top:9px">
         ${mini('Samples', total.toLocaleString(), total ? `${ok} ok · ${total - ok} failed` : 'none collected')}
         ${mini('Success', rate === null ? '—' : rate.toFixed(1) + '<small>%</small>', '')}
         ${mini('Latency p50', w.p50_latency_ms != null ? w.p50_latency_ms + '<small>ms</small>' : '—',
                w.max_latency_ms != null ? `max ${w.max_latency_ms} ms` : '')}
         ${mini('Payload', w.avg_bytes != null ? fmtBytes(w.avg_bytes) : '—', 'average response')}
       </div>

       <div class="col-sec">Definition</div>
       <dl class="col-kv">
         <dt>Endpoint</dt><dd><code>${esc(s.endpoint_path || '—')}</code>
           ${s.api_type ? `<span class="col-kind">${esc(s.api_type)}</span>` : ''}</dd>
         <dt>Device</dt><dd>${esc(s.device_name || '—')}
           ${s.device_target ? `<span class="col-tile-sub">${esc(s.device_target)}</span>` : ''}</dd>
         <dt>Site</dt><dd>${esc(s.site_name || '—')}</dd>
         <dt>Connector</dt><dd>${esc(s.connector_name || '—')}</dd>
         <dt>Credential</dt><dd>${esc(s.credential_name || '—')}</dd>
       </dl>

       ${last ? `<div class="col-sec">Last response</div>
       <dl class="col-kv">
         <dt>Outcome</dt><dd>${last.ok
           ? '<span class="col-state live">ok</span>'
           : `<span class="col-state failing">failed</span> ${esc(last.error || '')}`}</dd>
         <dt>HTTP status</dt><dd>${last.http_status != null ? last.http_status : '<span class="col-null">no reply</span>'}</dd>
         <dt>Latency</dt><dd>${last.latency_ms != null ? last.latency_ms + ' ms' : '—'}</dd>
         <dt>Size</dt><dd>${last.bytes != null ? fmtBytes(last.bytes) : '—'}${
           last.truncated ? ' <span class="col-state stale">— stored copy was cut at the 16 KB cap</span>' : ''}</dd>
       </dl>` : ''}`;
  }

  function samplesHtml(s) {
    const l = state.list;
    const filters = [['', 'All'], ['ok', 'Succeeded'], ['fail', 'Failed']].map(([v, label]) =>
      `<button class="col-chip ${l.status === v ? 'active' : ''}" data-sf="${v}" type="button">${label}</button>`).join('');

    if (l.loading && !l.rows.length) return `<div class="col-toolrow">${filters}</div>
      <div class="col-loading">Loading samples…</div>`;

    if (!l.rows.length) return `<div class="col-toolrow">${filters}</div>
      <div class="col-loading">No samples${l.status ? ' matching this filter' : ' recorded for this stream yet'}.</div>`;

    const rows = l.rows.map(r => `
      <button class="col-sample ${String(r.oid) === state.sampleOid ? 'on' : ''}" type="button"
              data-sample="${esc(r.oid)}">
        <span class="col-dot ${r.ok ? 'live' : 'failing'}"></span>
        <span style="min-width:0">
          <span class="col-sample-t">${esc(fmtTs(r.at))}</span>
          ${r.error ? `<span class="col-sample-e">${esc(r.error)}</span>` : ''}
        </span>
        <span class="col-num">${r.http_status != null ? r.http_status : '—'}</span>
        <span class="col-num">${r.latency_ms != null ? r.latency_ms + '<small>ms</small>' : '—'}</span>
        <span class="col-num">${r.bytes != null ? fmtBytes(r.bytes) : (r.body_aged ? '<small>aged</small>' : '—')}</span>
      </button>`).join('');

    return `<div class="col-toolrow">${filters}</div>
      <div class="col-samples">${rows}</div>
      <button class="col-more" id="colMore" type="button" ${l.cursor ? '' : 'disabled'}>${
        l.loading ? 'Loading…' : l.cursor ? 'Load older samples' : 'No older samples in retention'}</button>`;
  }

  // ── Payload views ───────────────────────────────────────────────────────────
  // Which sample is on screen only needs saying when it is not the latest one — otherwise the Data
  // and Raw tabs are simply "what this endpoint is returning", which is what the operator assumed.
  function payloadHeader(s) {
    const m = state.sample && state.sample.meta;
    if (!m || !s.last || String(s.last.oid) === String(m.oid)) return '';
    return `<div class="col-banner"><b>Showing an earlier sample</b>
      Collected ${esc(fmtTs(m.at))}, not the latest — picked on the Samples tab.</div>`;
  }

  function payloadGate(s) {
    if (state.sampleLoading) return `<div class="col-loading">Loading the response…</div>`;
    if (state.sampleErr) return `<div class="col-banner bad"><b>Could not load the sample</b>${esc(state.sampleErr)}</div>`;
    if (!state.sampleOid) return `<div class="col-loading">This stream has no sample to show yet.</div>`;
    const m = state.sample && state.sample.meta;
    if (!m) return `<div class="col-loading">No sample loaded.</div>`;
    if (m.body_aged) {
      return `<div class="col-banner warn"><b>The payload for this sample has been released</b>
        Its timings are kept for the full retention window, but only the most recent samples per
        stream keep their raw response. Open a newer sample to read a payload.</div>`;
    }
    if (state.sample.body == null || state.sample.body === '') {
      return `<div class="col-banner"><b>No response body</b>${
        m.ok ? 'The call succeeded but returned nothing.'
             : esc(m.error || 'The call failed before a response was received.')}</div>`;
    }
    return null;
  }

  function dataHtml(s) {
    const gate = payloadGate(s);
    if (gate !== null) return gate;

    const parsed = parsePayload(state.sample.body);
    if (parsed.kind === 'text') {
      return payloadHeader(s) + `<div class="col-banner warn"><b>Not a structured payload</b>${
        esc(parsed.error || 'The response is neither JSON nor XML, so there is nothing to derive a table from.')}
        Read it on the Raw tab.</div>`;
    }

    const found = findRecords(parsed.value);
    const head = payloadHeader(s) +
      `<dl class="col-kv"><dt>Format</dt><dd>${parsed.kind === 'xml' ? 'XML' : 'JSON'}${
        parsed.root ? ` · <code>&lt;${esc(parsed.root)}&gt;</code>` : ''}</dd>
        <dt>Records</dt><dd>${found.records
          ? `${found.records.length} at <code>${esc(found.path || 'root')}</code>`
          : '<span class="col-null">no repeating record set found</span>'}</dd></dl>`;

    if (!found.records || !found.records.length) {
      return head + `<div class="col-sec">Structure</div><div class="col-tree">${treeHtml(parsed.value)}</div>`;
    }

    return head +
      `<div class="col-sec">Records</div>${recordTableHtml(found.records)}
       <div class="col-sec">Full structure</div><div class="col-tree">${treeHtml(parsed.value)}</div>`;
  }

  // Raw is the stored response. Devices send it minified — one 16 KB line is unreadable — so it is
  // reflowed by default and the exact bytes are one click away. Reflowing only ever changes
  // whitespace, and where that is not good enough (a truncated body, a checksum, anything where the
  // literal bytes are the point) Exact is the same text untouched. Copy and Download always take
  // what is on screen, so what you paste is what you were reading.
  function rawHtml(s) {
    const gate = payloadGate(s);
    if (gate !== null) return gate;

    const m = state.sample.meta;
    const pretty = state.rawPretty ? formatBody(state.sample.body) : null;
    const shown = pretty ? pretty.text : state.sample.body;

    // The toggle is pointless on a payload that has no other form — offering it would imply the
    // page is withholding a nicer one.
    const canPretty = !!(pretty || formatBody(state.sample.body));
    const toggle = canPretty
      ? `<button class="col-btn" id="colFmt" type="button">${state.rawPretty ? 'Exact bytes' : 'Reflow'}</button>`
      : '';
    const failed = state.rawPretty && !pretty && m.truncated
      ? ' · cut mid-structure, so it cannot be reflowed'
      : '';

    return payloadHeader(s) +
      `<div class="col-toolrow">
         ${toggle}
         <button class="col-btn" id="colCopy" type="button">Copy</button>
         <button class="col-btn" id="colDownload" type="button">Download</button>
         <span class="col-stamp">${esc(fmtTs(m.at))} · ${m.bytes != null ? esc(fmtBytes(m.bytes)) : '—'}${
           m.truncated ? ' · cut at the 16 KB cap' : ''}${failed}${
           pretty ? ` · ${pretty.kind}, reflowed` : ''}</span>
       </div>
       <pre class="col-raw" id="colRaw">${esc(shown)}</pre>`;
  }

  // The on-screen text, whichever form is showing — what Copy and Download hand over.
  function shownBody() {
    const body = state.sample ? state.sample.body : '';
    if (!state.rawPretty) return body || '';
    const p = formatBody(body);
    return p ? p.text : (body || '');
  }

  // Reflow a payload for reading, or null when there is nothing to reflow (plain text, or a body cut
  // mid-structure by the collector's cap, which cannot parse and must not be guessed at).
  function formatBody(text) {
    const t = String(text == null ? '' : text).trim();
    if (!t) return null;

    if (t[0] === '{' || t[0] === '[') {
      try { return { text: JSON.stringify(JSON.parse(t), null, 2), kind: 'JSON' }; }
      catch (_) { return null; }
    }
    if (t[0] === '<') {
      const x = prettyXml(t);
      return x ? { text: x, kind: 'XML' } : null;
    }
    return null;
  }

  // Indent XML by nesting depth. An element whose entire content is text stays on one line —
  // <member>10.0.0.1</member> broken across three lines is more indentation than information.
  function prettyXml(src) {
    const doc = new DOMParser().parseFromString(src, 'application/xml');
    if (doc.querySelector('parsererror') || !doc.documentElement) return null;

    const tokens = src.replace(/\r?\n/g, '').replace(/>\s+</g, '><').split(/(<[^>]*>)/).filter(t => t.trim());
    const out = [];
    let depth = 0;

    for (let i = 0; i < tokens.length; i++) {
      const t = tokens[i].trim();
      const pad = '  '.repeat(depth);

      if (t.indexOf('</') === 0) {
        depth = Math.max(0, depth - 1);
        out.push('  '.repeat(depth) + t);
        continue;
      }
      if (t[0] !== '<') { out.push(pad + t); continue; }

      const selfClosing = t.slice(-2) === '/>' || t.indexOf('<?') === 0 || t.indexOf('<!') === 0;
      // <tag>text</tag> — collapse the three tokens onto one line.
      if (!selfClosing && tokens[i + 1] && tokens[i + 1][0] !== '<' &&
          tokens[i + 2] && tokens[i + 2].trim().indexOf('</') === 0) {
        out.push(pad + t + tokens[i + 1].trim() + tokens[i + 2].trim());
        i += 2;
        continue;
      }
      out.push(pad + t);
      if (!selfClosing) depth++;
    }

    return out.join('\n');
  }

  function onDrawerClick(e) {
    const sf = e.target.closest('button[data-sf]');
    if (sf) {
      state.list.status = sf.dataset.sf;
      loadSamples(true);
      return;
    }
    if (e.target.closest('#colMore')) { loadSamples(false); return; }

    const sample = e.target.closest('button[data-sample]');
    if (sample) {
      state.sampleOid = sample.dataset.sample;
      state.tab = 'data';
      loadSample(state.sampleOid);
      paintDrawer();
      return;
    }

    if (e.target.closest('#colFmt')) {
      state.rawPretty = !state.rawPretty;
      paintDrawer();
      return;
    }

    const copy = e.target.closest('#colCopy');
    if (copy) {
      navigator.clipboard?.writeText(shownBody());
      copy.textContent = 'Copied';
      setTimeout(() => { copy.textContent = 'Copy'; }, 1400);
      return;
    }
    if (e.target.closest('#colDownload')) {
      const s = byKey(state.sel);
      const blob = new Blob([shownBody()], { type: 'text/plain' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `${(s?.endpoint_name || 'sample').replace(/\W+/g, '-')}-${state.sampleOid}.txt`;
      a.click();
      URL.revokeObjectURL(a.href);
    }
  }

  // ── Sample fetches ──────────────────────────────────────────────────────────
  async function loadSample(oid) {
    state.sampleLoading = true;
    state.sampleErr = '';
    if (state.tab === 'data' || state.tab === 'raw') paintDrawer();
    try {
      const d = await window.NMS.utils.fetchJSON('/api/collection/sample?oid=' + encodeURIComponent(oid));
      if (!d) return;
      state.sample = { meta: d, body: d.body };
    } catch (e) {
      state.sample = null;
      state.sampleErr = 'the appliance did not return it (' + e.message + ')';
    }
    state.sampleLoading = false;
    paintDrawer();
  }

  async function loadSamples(reset) {
    const s = byKey(state.sel);
    if (!s || state.list.loading) return;
    if (reset) state.list = { rows: [], cursor: null, status: state.list.status, loading: true };
    else state.list.loading = true;
    paintDrawer();

    const qs = new URLSearchParams({
      connector: s.connector_oid, endpoint: s.endpoint_oid, limit: String(SAMPLE_PAGE),
    });
    if (state.list.status) qs.set('status', state.list.status);
    if (!reset && state.list.cursor) qs.set('before', state.list.cursor);

    try {
      const d = await window.NMS.utils.fetchJSON('/api/collection/samples?' + qs.toString());
      if (d) {
        state.list.rows = state.list.rows.concat(d.rows || []);
        state.list.cursor = d.next_cursor || null;
      }
    } catch (_) {
      state.list.cursor = null;
    }
    state.list.loading = false;
    paintDrawer();
  }

  // ── Payload shaping ─────────────────────────────────────────────────────────
  // Everything below is a best-effort reading of an unknown schema. It never mutates and never hides
  // the original — Raw is one tab away, and any disagreement between the two is resolved in Raw's
  // favour.

  function parsePayload(text) {
    const t = String(text == null ? '' : text).trim();
    if (!t) return { kind: 'text', value: '' };

    if (t[0] === '{' || t[0] === '[') {
      try { return { kind: 'json', value: JSON.parse(t) }; }
      catch (e) {
        return { kind: 'text', value: text,
                 error: 'the response starts like JSON but does not parse — ' + e.message };
      }
    }

    if (t[0] === '<') {
      try {
        const doc = new DOMParser().parseFromString(t, 'application/xml');
        if (doc.querySelector('parsererror') || !doc.documentElement)
          return { kind: 'text', value: text, error: 'the response is XML but not well-formed' };
        return { kind: 'xml', value: xmlToObj(doc.documentElement), root: doc.documentElement.nodeName };
      } catch (_) {
        return { kind: 'text', value: text, error: 'the XML response could not be parsed' };
      }
    }

    return { kind: 'text', value: text };
  }

  // XML → plain object, in the convention PAN-OS responses are usually read with: attributes carry an
  // `@` prefix, repeated siblings collapse into an array, an element that is only text becomes that
  // text. Lossless enough for a reading, and the Raw tab holds the original either way.
  function xmlToObj(el) {
    const out = {};
    for (let i = 0; i < el.attributes.length; i++) out['@' + el.attributes[i].name] = el.attributes[i].value;

    let text = '';
    const kids = [];
    el.childNodes.forEach(n => {
      if (n.nodeType === 3 || n.nodeType === 4) text += n.nodeValue;
      else if (n.nodeType === 1) kids.push(n);
    });

    if (!kids.length) {
      const t = text.trim();
      if (!Object.keys(out).length) return t;
      if (t) out['#text'] = t;
      return out;
    }

    kids.forEach(k => {
      const v = xmlToObj(k);
      if (out[k.nodeName] === undefined) out[k.nodeName] = v;
      else if (Array.isArray(out[k.nodeName])) out[k.nodeName].push(v);
      else out[k.nodeName] = [out[k.nodeName], v];
    });
    const t = text.trim();
    if (t) out['#text'] = t;
    return out;
  }

  const isRecord = (v) => v && typeof v === 'object' && !Array.isArray(v);

  // Where the repeating rows live. The named paths are tried first because PAN-OS puts them in the
  // same two places every time and guessing would only ever agree with them; the sweep afterwards is
  // for every other API, and simply takes the biggest array of objects it can find.
  const KNOWN_PATHS = [
    ['result', 'entry'], ['response', 'result', 'entry'], ['result', 'members', 'entry'],
    ['result', 'rules', 'entry'], ['entry'], ['result'], ['data'], ['items'], ['records'], ['rows'],
  ];

  function findRecords(root) {
    const at = (obj, path) => path.reduce((o, k) => (o && typeof o === 'object') ? o[k] : undefined, obj);

    for (const p of KNOWN_PATHS) {
      const v = at(root, p);
      if (Array.isArray(v) && v.some(isRecord)) return { records: v.filter(isRecord), path: p.join('.') };
      if (isRecord(v) && Object.keys(v).length && !Object.values(v).some(x => Array.isArray(x) && x.some(isRecord)))
        return { records: [v], path: p.join('.') };
    }

    let best = null;
    const walk = (node, path, depth) => {
      if (depth > 6 || !node || typeof node !== 'object') return;
      if (Array.isArray(node)) {
        const recs = node.filter(isRecord);
        if (recs.length && (!best || recs.length > best.records.length))
          best = { records: recs, path: path || 'root' };
        node.slice(0, 5).forEach((v, i) => walk(v, `${path}[${i}]`, depth + 1));
        return;
      }
      Object.keys(node).forEach(k => walk(node[k], path ? path + '.' + k : k, depth + 1));
    };
    walk(root, '', 0);

    return best || { records: null, path: '' };
  }

  // Columns are the union of the records' keys. Identity-ish fields lead — a table whose first column
  // is an internal flag reads as noise however correct it is — and the rest follow by how many
  // records actually carry them, so sparse extras sink to the right instead of pushing rows wide.
  const LEAD_KEYS = ['@name', 'name', '@uuid', 'uuid', 'id', '@id', '@loc', '@location', 'location',
                     '@vsys', 'vsys', 'type', '@type', 'description'];

  function columnsOf(records) {
    const freq = new Map();
    records.slice(0, 200).forEach(r => Object.keys(r).forEach(k => freq.set(k, (freq.get(k) || 0) + 1)));

    const keys = [...freq.keys()];
    const lead = LEAD_KEYS.filter(k => freq.has(k));
    const rest = keys.filter(k => !lead.includes(k))
      .sort((a, b) => (freq.get(b) - freq.get(a)) || a.localeCompare(b));

    return { cols: lead.concat(rest).slice(0, MAX_TABLE_COLS), total: keys.length };
  }

  function cellHtml(v) {
    if (v === null || v === undefined || v === '') return `<span class="col-null">—</span>`;
    if (typeof v !== 'object') return esc(String(v));

    // PAN-OS wraps every list of scalars as { member: [...] }; unwrapping it is the difference
    // between a readable cell and a chip that has to be opened to learn it holds two names.
    if (!Array.isArray(v) && v.member !== undefined) {
      const m = Array.isArray(v.member) ? v.member : [v.member];
      if (m.every(x => typeof x !== 'object')) return esc(m.join(', '));
    }
    if (!Array.isArray(v) && Object.keys(v).length === 1) {
      const [k] = Object.keys(v);
      // XML text node — the wrapper carries nothing.
      if (k === '#text') return esc(String(v[k]));
      // A lone scalar under a lone key ({"enable":"no"}) says more spelled out than a chip reading
      // "{1}" that has to be hovered to learn the same thing.
      if (v[k] === null || typeof v[k] !== 'object') return esc(`${k}=${v[k]}`);
    }

    const json = JSON.stringify(v);
    const label = Array.isArray(v) ? `[${v.length}]` : `{${Object.keys(v).length}}`;
    return `<span class="col-cellchip" title="${esc(json.slice(0, 600))}">${label}</span>`;
  }

  function recordTableHtml(records) {
    const { cols, total } = columnsOf(records);
    if (!cols.length) return `<div class="col-loading">The records carry no fields to tabulate.</div>`;

    const shown = records.slice(0, MAX_TABLE_ROWS);
    const head = cols.map(c => `<th title="${esc(c)}">${esc(c)}</th>`).join('');
    const body = shown.map(r =>
      `<tr>${cols.map(c => `<td title="${esc(typeof r[c] === 'object' ? '' : String(r[c] ?? ''))}">${
        cellHtml(r[c])}</td>`).join('')}</tr>`).join('');

    const more = records.length > shown.length
      ? `<div class="col-note">Showing ${shown.length} of ${records.length} records — the rest are in Raw.</div>`
      : '';
    const cut = total > cols.length
      ? `<div class="col-note">${total - cols.length} further field${total - cols.length === 1 ? '' : 's'}
         are not columns here; every one of them is in the structure below.</div>`
      : '';

    return `<div class="col-table-wrap"><table class="col-table">
        <thead><tr>${head}</tr></thead><tbody>${body}</tbody></table></div>${more}${cut}`;
  }

  // Collapsible structure view. Depth-first with a node budget: an address-object dump can carry
  // thousands of nodes, and a page that locks up rendering them is worse than one that says so.
  function treeHtml(value) {
    let budget = MAX_TREE_NODES;

    const scalar = (v) => {
      if (v === null) return `<span class="z">null</span>`;
      if (typeof v === 'string') return `<span class="s">"${esc(v.length > 200 ? v.slice(0, 200) + '…' : v)}"</span>`;
      if (typeof v === 'number') return `<span class="n">${esc(v)}</span>`;
      if (typeof v === 'boolean') return `<span class="b">${v}</span>`;
      return esc(String(v));
    };

    const walk = (v, key, depth) => {
      if (budget-- <= 0) return '';
      const label = key === null ? '' : `<span class="k">${esc(key)}</span>: `;

      if (v === null || typeof v !== 'object') return `<div class="leaf">${label}${scalar(v)}</div>`;

      const entries = Array.isArray(v)
        ? v.map((x, i) => [String(i), x])
        : Object.keys(v).map(k => [k, v[k]]);

      if (!entries.length) return `<div class="leaf">${label}<span class="z">${Array.isArray(v) ? '[]' : '{}'}</span></div>`;

      const summary = `${label}<span class="z">${Array.isArray(v) ? `[${v.length}]` : `{${entries.length}}`}</span>`;
      // Only the first two levels start open: deeper than that and the point of a tree is lost.
      return `<details ${depth < 2 ? 'open' : ''}><summary>${summary}</summary>${
        entries.map(([k, x]) => walk(x, k, depth + 1)).join('')}</details>`;
    };

    const html = walk(value, null, 0);
    return budget <= 0
      ? html + `<div class="col-note">Structure truncated — the full response is on the Raw tab.</div>`
      : html;
  }

  // ── Formatting ──────────────────────────────────────────────────────────────
  const fmtTs = (s) => window.NMS.utils.fmtTs(s) === '—' ? (s || '—') : window.NMS.utils.fmtTs(s);

  function fmtBytes(n) {
    const v = Number(n);
    if (!isFinite(v)) return '—';
    if (v < 1024) return v + ' B';
    if (v < 1024 * 1024) return (v / 1024).toFixed(v < 10240 ? 1 : 0) + ' KB';
    return (v / 1048576).toFixed(1) + ' MB';
  }

  // ── Lifecycle ───────────────────────────────────────────────────────────────
  function schedule() {
    clearInterval(timer);
    timer = null;
    if (state.live) timer = setInterval(load, REFRESH_MS);
  }

  document.addEventListener('DOMContentLoaded', () => {
    mount();
    load();
    schedule();
    if (window.NMS && window.NMS.onRefresh) window.NMS.onRefresh(load);
  });
}());
