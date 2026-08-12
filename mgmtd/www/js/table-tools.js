/* table-tools.js — sorting, per-column filtering and search for the Configuration tables.
 *
 * Every Configuration tab renders the same thing: a list of declared objects, one row each. They
 * were built as static tables, which is fine at five rows and useless at five hundred — an operator
 * with a real estate cannot find the one device they came for. This module is the one place that
 * answers "narrow it down", so Sites, Devices, API Credential, API Endpoint and API Connector all
 * behave identically rather than each growing its own half of the feature.
 *
 * A tab declares its columns and hands over the rows; everything below — the header sort affordance,
 * the per-column filter popover, the search box, the active-filter chips and the row count — is
 * rendered and wired here.
 *
 *   NMS.table.create({
 *     id:      'devices',                 // persistence key (per browser tab)
 *     columns: [ … ],                     // see COLUMN below
 *     empty:   '<div class=cfg-empty>…',  // shown when there are no rows at all
 *     onRows:  (tbody) => …,              // re-wire row buttons after every body paint
 *     rowClass:(row, i) => '',            // optional per-row class
 *   })
 *
 * COLUMN
 *   key      stable id — the persistence key for this column's sort/filter, so renaming it drops
 *            whatever the operator had set. Not the label.
 *   label    header text ('' for the actions column)
 *   cls      class put on both the th and the td, so the existing width rules keep working
 *   text     (row, i) => plain string. The value the column is searched, filtered and — unless
 *            sortValue says otherwise — sorted by. MUST return '' for "nothing here", which is what
 *            makes "is empty" and empties-sort-last correct.
 *   cell     (row, i) => HTML for the td. Defaults to the escaped text.
 *   sortValue(row, i) => number|string, when the thing to order by is not the thing on screen
 *            (an expiry reads "5m" but sorts by the instant it happens).
 *   filter   'text' | 'enum' | 'number' | false
 *   sort     false to make the column unsortable (default true when it has text)
 *   search   false to keep the column out of the search box (default true when it has text)
 *   searchText(row, i) => string, when the cell shows more than the column is sorted and filtered by
 *            (a name cell carrying a description under it: the column is "Name", but nobody typing
 *            a word from that description expects the search to miss the row).
 *
 * Sort is tri-state per column — ascending, descending, off — and shift-click adds a second and
 * third key rather than replacing the first, which is what makes "by site, then by name" possible.
 * Empty cells sort last in BOTH directions: a blank is missing information, not a small value, and
 * flipping the direction to get away from a block of blanks would be absurd.
 *
 * The view (search + sort + filters) is kept in sessionStorage per browser tab. Configuration
 * switches tabs client-side and re-renders on every save, so anything less would throw the
 * operator's narrowing away several times a minute.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};
  const { esc } = window.NMS.utils;

  // Natural ordering: "fw-10" comes after "fw-9", and capitalisation is not a distinction the
  // operator meant to make when they named two devices.
  const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' });

  const SVG_FUNNEL = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"
      stroke-linecap="round" stroke-linejoin="round"><polygon points="21 4 3 4 10.5 12.6 10.5 19 13.5 20.5 13.5 12.6 21 4"/></svg>`;
  const SVG_SEARCH = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
      stroke-linecap="round"><circle cx="11" cy="11" r="7"/><line x1="20" y1="20" x2="16.65" y2="16.65"/></svg>`;

  // The operators an operator actually reaches for. "is empty" / "is not empty" earn their place
  // because a missing reference is the most common thing worth hunting for in this configuration.
  const TEXT_OPS = [
    ['contains',     'contains'],
    ['not_contains', 'does not contain'],
    ['equals',       'is'],
    ['starts',       'starts with'],
    ['ends',         'ends with'],
    ['empty',        'is empty'],
    ['not_empty',    'is not empty'],
  ];
  const opLabel = (op) => (TEXT_OPS.find(([k]) => k === op) || TEXT_OPS[0])[1];
  const opNeedsValue = (op) => op !== 'empty' && op !== 'not_empty';

  // ── View persistence ────────────────────────────────────────────────────────
  // Per browser tab, next to the staged drafts (js/commit.js): a narrowed table is part of what the
  // operator is doing right now, not a preference to carry to the next session.
  const storeKey = (id) => 'pz.tbl.' + id;

  function loadView(id) {
    try { return JSON.parse(sessionStorage.getItem(storeKey(id)) || 'null'); } catch (_) { return null; }
  }
  function saveView(id, view) {
    try {
      const empty = !view.q && !view.sort.length && !Object.keys(view.filters).length;
      if (empty) sessionStorage.removeItem(storeKey(id));
      else sessionStorage.setItem(storeKey(id), JSON.stringify(view));
    } catch (_) { /* private mode — the view just does not survive a reload */ }
  }

  // ── Comparison ──────────────────────────────────────────────────────────────
  const isBlank = (v) => v === null || v === undefined || v === '';

  function cmp(a, b) {
    // Blanks last, whichever way the column is pointing (see the header comment).
    if (isBlank(a) || isBlank(b)) return isBlank(a) && isBlank(b) ? 0 : (isBlank(a) ? 1 : -1);
    if (typeof a === 'number' && typeof b === 'number') return a - b;
    if (typeof a === 'boolean' || typeof b === 'boolean') return (a ? 1 : 0) - (b ? 1 : 0);
    return collator.compare(String(a), String(b));
  }

  // Applied to the non-blank comparison only, so direction never drags blanks to the top.
  function cmpDir(a, b, dir) {
    if (isBlank(a) || isBlank(b)) return cmp(a, b);
    return dir === 'desc' ? -cmp(a, b) : cmp(a, b);
  }

  // ── The filter popover ──────────────────────────────────────────────────────
  // One shared element on <body>, like the nav flyout and the connector hover card: the table has
  // its own overflow and would crop a panel mounted inside it.
  let popEl = null;
  let popClose = null;   // closes whatever is open, so a second header cannot open a second panel

  function popRoot() {
    if (!popEl) {
      popEl = document.createElement('div');
      popEl.className = 'tf-pop';
      popEl.id = 'tblFilterPop';
      document.body.appendChild(popEl);
    }
    return popEl;
  }

  function positionPop(anchor) {
    const pop = popRoot();
    const r = anchor.getBoundingClientRect();
    // Anchored under the header cell, right-aligned to it so a filter on the last column stays on
    // screen; clamped to the viewport either way.
    const pw = pop.offsetWidth, ph = pop.offsetHeight;
    let left = Math.min(r.left, window.innerWidth - pw - 8);
    left = Math.max(8, left);
    let top = r.bottom + 6;
    if (top + ph > window.innerHeight - 8 && r.top - 6 - ph > 8) top = r.top - 6 - ph;
    pop.style.left = left + 'px';
    pop.style.top = Math.max(8, top) + 'px';
  }

  function closePop() { if (popClose) popClose(); }

  // ── Instance ────────────────────────────────────────────────────────────────
  function create(spec) {
    const id = spec.id;
    const columns = (spec.columns || []).filter(Boolean);
    const colOf = (key) => columns.find(c => c.key === key) || null;

    const canSort   = (c) => !!c && c.sort !== false && typeof c.text === 'function';
    const canFilter = (c) => !!c && !!c.filter && typeof c.text === 'function';
    const canSearch = (c) => !!c && c.search !== false && typeof c.text === 'function';

    // View state, restored from the store but validated against the columns as they are NOW: a
    // column that was renamed or dropped must not leave an invisible filter narrowing the table.
    const view = { q: '', sort: [], filters: {} };
    (function restore() {
      const saved = loadView(id);
      if (!saved) return;
      if (typeof saved.q === 'string') view.q = saved.q;
      if (Array.isArray(saved.sort)) {
        view.sort = saved.sort
          .filter(s => s && canSort(colOf(s.key)))
          .map(s => ({ key: s.key, dir: s.dir === 'desc' ? 'desc' : 'asc' }));
      }
      if (saved.filters && typeof saved.filters === 'object') {
        Object.keys(saved.filters).forEach(k => {
          const c = colOf(k);
          const f = saved.filters[k];
          if (canFilter(c) && f && f.type === c.filter) view.filters[k] = f;
        });
      }
    })();

    const persist = () => saveView(id, view);

    let entries = [];    // [{ row, i, t }] in the order the tab supplied them
    let host = null;     // the element the table is mounted in

    // Column text is asked for repeatedly in one paint (search, every filter, the sort, the cell) and
    // often resolves a cross-module reference to do it, so it is computed once per row per paint.
    function textOf(col, e) {
      if (!(col.key in e.t)) {
        let v = '';
        try { v = col.text(e.row, e.i); } catch (_) { v = ''; }
        e.t[col.key] = (v === null || v === undefined) ? '' : String(v);
      }
      return e.t[col.key];
    }

    // What the search box reads for this column — the cell's full text when it carries more than the
    // column's own value. Cached under a key that cannot collide with a column key.
    function searchTextOf(col, e) {
      if (typeof col.searchText !== 'function') return textOf(col, e);
      const k = '~s:' + col.key;
      if (!(k in e.t)) {
        let v = '';
        try { v = col.searchText(e.row, e.i); } catch (_) { v = ''; }
        e.t[k] = (v === null || v === undefined) ? '' : String(v);
      }
      return e.t[k];
    }

    function sortOf(col, e) {
      if (typeof col.sortValue !== 'function') return textOf(col, e);
      try {
        const v = col.sortValue(e.row, e.i);
        return (v === undefined) ? '' : v;
      } catch (_) { return ''; }
    }

    // ── Matching ──────────────────────────────────────────────────────────────
    function matchText(hay, f) {
      const h = String(hay || '').toLowerCase();
      if (f.op === 'empty') return h === '';
      if (f.op === 'not_empty') return h !== '';
      const v = String(f.v || '').trim().toLowerCase();
      if (!v) return true;
      switch (f.op) {
        case 'not_contains': return h.indexOf(v) === -1;
        case 'equals':       return h === v;
        case 'starts':       return h.indexOf(v) === 0;
        case 'ends':         return h.length >= v.length && h.lastIndexOf(v) === h.length - v.length;
        default:             return h.indexOf(v) !== -1;
      }
    }

    function matchNumber(col, e, f) {
      const raw = sortOf(col, e);
      const n = typeof raw === 'number' ? raw : parseFloat(raw);
      if (!isFinite(n)) return false;   // "no value" cannot be inside a range
      if (f.min !== '' && f.min !== undefined && n < parseFloat(f.min)) return false;
      if (f.max !== '' && f.max !== undefined && n > parseFloat(f.max)) return false;
      return true;
    }

    function matchFilter(key, e) {
      const f = view.filters[key];
      const col = colOf(key);
      if (!f || !col) return true;
      if (f.type === 'enum') return !f.sel || !f.sel.length || f.sel.indexOf(textOf(col, e)) !== -1;
      if (f.type === 'number') return matchNumber(col, e, f);
      return matchText(textOf(col, e), f);
    }

    // Every term must match somewhere in the row — typing "seoul fw" finds the Seoul firewall
    // rather than everything in Seoul plus every firewall.
    function matchSearch(e) {
      const terms = view.q.trim().toLowerCase().split(/\s+/).filter(Boolean);
      if (!terms.length) return true;
      const hay = columns.filter(canSearch).map(c => searchTextOf(c, e)).join('  ').toLowerCase();
      return terms.every(t => hay.indexOf(t) !== -1);
    }

    // `exceptKey` is what makes a filter panel list the values that are still reachable WITHOUT
    // hiding the ones the operator already ticked in that same panel.
    function passes(e, exceptKey) {
      if (!matchSearch(e)) return false;
      return Object.keys(view.filters).every(k => k === exceptKey || matchFilter(k, e));
    }

    function computeVisible() {
      const out = entries.filter(e => passes(e, null));
      if (view.sort.length) {
        const keys = view.sort.map(s => ({ col: colOf(s.key), dir: s.dir })).filter(s => s.col);
        out.sort((a, b) => {
          for (const s of keys) {
            const r = cmpDir(sortOf(s.col, a), sortOf(s.col, b), s.dir);
            if (r) return r;
          }
          return a.i - b.i;   // stable: equal rows keep the order the tab supplied
        });
      }
      return out;
    }

    // Distinct values of one column over the rows the OTHER filters leave, each with the count it
    // would bring back. An option that can only ever return nothing is not offered.
    function optionsFor(col) {
      const counts = new Map();
      entries.forEach(e => {
        if (!passes(e, col.key)) return;
        const v = textOf(col, e);
        counts.set(v, (counts.get(v) || 0) + 1);
      });
      return [...counts.entries()]
        .sort((a, b) => cmp(a[0] === '' ? null : a[0], b[0] === '' ? null : b[0]));
    }

    // ── Filter mutation ───────────────────────────────────────────────────────
    function setFilter(key, f) {
      if (f) view.filters[key] = f;
      else delete view.filters[key];
      persist();
      paint();
    }

    const activeFilterKeys = () => Object.keys(view.filters);

    function filterSummary(key) {
      const f = view.filters[key];
      const col = colOf(key);
      if (!f || !col) return '';
      if (f.type === 'enum') {
        const sel = f.sel || [];
        const shown = sel.slice(0, 2).map(v => v === '' ? '(empty)' : v).join(', ');
        return sel.length > 2 ? `${shown} +${sel.length - 2}` : shown;
      }
      if (f.type === 'number') {
        if (f.min !== '' && f.max !== '') return `${f.min} – ${f.max}`;
        if (f.min !== '') return `≥ ${f.min}`;
        return `≤ ${f.max}`;
      }
      return opNeedsValue(f.op) ? `${opLabel(f.op)} "${f.v}"` : opLabel(f.op);
    }

    // A filter that cannot narrow anything is not a filter — dropping it here keeps the chip row and
    // the "clear" affordance honest instead of showing an empty condition.
    function normalizeFilter(key, f) {
      if (!f) return null;
      if (f.type === 'enum') return (f.sel && f.sel.length) ? f : null;
      if (f.type === 'number') return (f.min !== '' || f.max !== '') ? f : null;
      if (!opNeedsValue(f.op)) return f;
      return String(f.v || '').trim() ? f : null;
    }

    // ── Sorting ───────────────────────────────────────────────────────────────
    // Ascending → descending → off. Off matters: it is how the operator gets back to the order the
    // tab is stored in, which for a hand-ordered list is itself information.
    function toggleSort(key, additive) {
      const col = colOf(key);
      if (!canSort(col)) return;
      const at = view.sort.findIndex(s => s.key === key);
      const cur = at === -1 ? null : view.sort[at];
      const next = !cur ? 'asc' : (cur.dir === 'asc' ? 'desc' : null);

      if (!additive) view.sort = next ? [{ key, dir: next }] : [];
      else if (!cur) view.sort.push({ key, dir: 'asc' });
      else if (next) view.sort[at].dir = next;
      else view.sort.splice(at, 1);

      persist();
      paint();
    }

    function sortStateOf(key) {
      const at = view.sort.findIndex(s => s.key === key);
      return at === -1 ? null : { dir: view.sort[at].dir, ord: at + 1, multi: view.sort.length > 1 };
    }

    // ── Painting ──────────────────────────────────────────────────────────────
    function headHtml() {
      return '<tr>' + columns.map(c => {
        const sortable = canSort(c);
        const filterable = canFilter(c);
        const s = sortStateOf(c.key);
        const cls = [
          c.cls || '',
          sortable ? 'th-sortable' : '',
          s ? 'th-sorted th-' + s.dir : '',
          view.filters[c.key] ? 'th-filtered' : '',
        ].filter(Boolean).join(' ');
        const aria = s ? (s.dir === 'asc' ? 'ascending' : 'descending') : (sortable ? 'none' : '');

        if (!sortable && !filterable) return `<th class="${esc(cls)}">${esc(c.label || '')}</th>`;

        const arrow = sortable
          ? `<span class="th-arrow" aria-hidden="true"></span>${
              s && s.multi ? `<span class="th-ord">${s.ord}</span>` : ''}`
          : '';
        const funnel = filterable
          ? `<button type="button" class="th-filter" data-filter="${esc(c.key)}"
                     aria-label="Filter by ${esc(c.label || c.key)}"
                     title="Filter by ${esc(c.label || c.key)}">${SVG_FUNNEL}</button>`
          : '';

        return `<th class="${esc(cls)}" data-k="${esc(c.key)}"
                    ${sortable ? 'tabindex="0" role="columnheader"' : ''}
                    ${aria ? `aria-sort="${aria}"` : ''}
                    ${sortable ? `title="Sort by ${esc(c.label || c.key)} — shift-click to add a second key"` : ''}>
            <span class="th-in"><span class="th-label">${esc(c.label || '')}</span>${arrow}${funnel}</span>
          </th>`;
      }).join('') + '</tr>';
    }

    function bodyHtml(visible) {
      if (!entries.length) {
        // A function when the message depends on what else is configured (see api-connectors.js).
        const empty = (typeof spec.empty === 'function') ? spec.empty() : (spec.empty || '');
        return `<tr class="tbl-empty-row"><td colspan="${columns.length}">${empty}</td></tr>`;
      }
      if (!visible.length) {
        return `<tr class="tbl-empty-row"><td colspan="${columns.length}">
            <div class="cfg-empty">Nothing matches the current search and filters.
              <button type="button" class="tbl-link" data-reset>Clear them</button></div>
          </td></tr>`;
      }
      return visible.map(e => {
        const extra = spec.rowClass ? (spec.rowClass(e.row, e.i) || '') : '';
        const tds = columns.map(c => {
          let inner;
          if (typeof c.cell === 'function') inner = c.cell(e.row, e.i);
          else {
            const t = textOf(c, e);
            inner = t ? esc(t) : '<span class="muted">—</span>';
          }
          return `<td class="${esc(c.cls || '')}">${inner}</td>`;
        }).join('');
        return `<tr${extra ? ` class="${esc(extra)}"` : ''}>${tds}</tr>`;
      }).join('');
    }

    function barHtml() {
      return `
        <div class="tbl-bar">
          <div class="tbl-search">${SVG_SEARCH}
            <input type="search" class="tbl-q" placeholder="${esc(spec.searchPlaceholder || 'Search…')}"
                   value="${esc(view.q)}" spellcheck="false" autocomplete="off" aria-label="Search this table"/>
          </div>
          <div class="tbl-chips"></div>
          <span class="tbl-spacer"></span>
          <span class="tbl-count"></span>
          <button type="button" class="tbl-reset" data-reset>Clear</button>
        </div>`;
    }

    function chipsHtml() {
      const chips = [];
      view.sort.forEach((s, n) => {
        const col = colOf(s.key);
        if (!col) return;
        chips.push(`<span class="tbl-chip tbl-chip-sort" data-unsort="${esc(s.key)}" role="button" tabindex="0"
              title="Remove this sort key">
            <span class="tbl-chip-k">${view.sort.length > 1 ? (n + 1) + '. ' : ''}${esc(col.label || s.key)}</span>
            <span class="tbl-chip-v">${s.dir === 'asc' ? '↑' : '↓'}</span>
            <span class="tbl-chip-x" aria-hidden="true">&times;</span>
          </span>`);
      });
      activeFilterKeys().forEach(k => {
        const col = colOf(k);
        if (!col) return;
        chips.push(`<span class="tbl-chip" data-unfilter="${esc(k)}" role="button" tabindex="0"
              title="Remove this filter">
            <span class="tbl-chip-k">${esc(col.label || k)}</span>
            <span class="tbl-chip-v">${esc(filterSummary(k))}</span>
            <span class="tbl-chip-x" aria-hidden="true">&times;</span>
          </span>`);
      });
      return chips.join('');
    }

    function paint() {
      if (!host) return;
      entries.forEach(e => { e.t = {}; });   // cross-module labels may have moved since the last paint

      const visible = computeVisible();
      const narrowed = visible.length !== entries.length;

      const thead = host.querySelector('thead');
      const tbody = host.querySelector('tbody');
      if (thead) thead.innerHTML = headHtml();
      if (tbody) tbody.innerHTML = bodyHtml(visible);

      const chips = host.querySelector('.tbl-chips');
      if (chips) chips.innerHTML = chipsHtml();

      const count = host.querySelector('.tbl-count');
      if (count) {
        count.textContent = !entries.length ? ''
          : narrowed ? `${visible.length} of ${entries.length}`
                     : `${entries.length} row${entries.length === 1 ? '' : 's'}`;
        count.classList.toggle('is-narrowed', narrowed);
      }

      const dirty = !!(view.q || view.sort.length || activeFilterKeys().length);
      host.querySelector('.tbl-bar')?.classList.toggle('is-active', dirty);
      const reset = host.querySelector('.tbl-reset');
      if (reset) reset.hidden = !dirty;

      // The tab owns its row buttons; they are inside a body this module just replaced.
      if (tbody && typeof spec.onRows === 'function') spec.onRows(tbody);
    }

    function clearAll() {
      view.q = '';
      view.sort = [];
      view.filters = {};
      persist();
      const q = host && host.querySelector('.tbl-q');
      if (q) q.value = '';
      paint();
    }

    // ── Filter panel ──────────────────────────────────────────────────────────
    function openFilter(key, anchor) {
      const col = colOf(key);
      if (!col) return;
      const wasOpen = popEl && popEl.classList.contains('open') && popEl.dataset.k === key && popEl.dataset.id === id;
      closePop();
      if (wasOpen) return;   // clicking the same funnel again closes it

      const pop = popRoot();
      pop.dataset.k = key;
      pop.dataset.id = id;
      pop.innerHTML = panelHtml(col);
      pop.classList.add('open');
      positionPop(anchor);
      wirePanel(col, anchor);

      const onDocDown = (e) => { if (!pop.contains(e.target) && e.target !== anchor && !anchor.contains(e.target)) closePop(); };
      const onKey = (e) => { if (e.key === 'Escape') { e.preventDefault(); closePop(); anchor.focus(); } };
      // A panel with its own scrolling list must not close when that list is scrolled; the anchor
      // moving out from under it is a different matter.
      const onScroll = (e) => {
        if (pop.contains(e.target)) return;
        const r = anchor.getBoundingClientRect();
        if (r.bottom < 0 || r.top > window.innerHeight) closePop();
        else positionPop(anchor);
      };

      popClose = () => {
        pop.classList.remove('open');
        pop.innerHTML = '';
        delete pop.dataset.k;
        delete pop.dataset.id;
        document.removeEventListener('mousedown', onDocDown, true);
        document.removeEventListener('keydown', onKey, true);
        window.removeEventListener('scroll', onScroll, true);
        window.removeEventListener('resize', closePop);
        popClose = null;
      };

      document.addEventListener('mousedown', onDocDown, true);
      document.addEventListener('keydown', onKey, true);
      window.addEventListener('scroll', onScroll, true);
      window.addEventListener('resize', closePop);

      pop.querySelector('.tf-find, .tf-v, .tf-num')?.focus();
    }

    function panelHtml(col) {
      const f = view.filters[col.key];
      const sortRow = canSort(col) ? `
        <div class="tf-sorts">
          <button type="button" class="tf-sort" data-sort="asc">Sort A → Z</button>
          <button type="button" class="tf-sort" data-sort="desc">Sort Z → A</button>
          <button type="button" class="tf-sort" data-sort="none"
                  ${sortStateOf(col.key) ? '' : 'disabled'}>Unsort</button>
        </div>` : '';

      let bodyHtmlStr = '';
      if (col.filter === 'enum') {
        const opts = optionsFor(col);
        const sel = (f && f.sel) || [];
        const all = opts.length && opts.every(([v]) => sel.indexOf(v) !== -1);
        bodyHtmlStr = `
          ${opts.length > 8 ? `<input type="search" class="tf-find" placeholder="Find value…" autocomplete="off"/>` : ''}
          <label class="tf-opt tf-opt-all">
            <input type="checkbox" data-all ${all ? 'checked' : ''}/>
            <span class="tf-v-label">(Select all)</span>
          </label>
          <div class="tf-list">${
            opts.length
              ? opts.map(([v, n]) => `
                  <label class="tf-opt" data-v="${esc(v.toLowerCase())}">
                    <input type="checkbox" data-val="${esc(v)}" ${sel.indexOf(v) !== -1 ? 'checked' : ''}/>
                    <span class="tf-v-label">${v === '' ? '<i>(empty)</i>' : esc(v)}</span>
                    <span class="tf-n">${n}</span>
                  </label>`).join('')
              : '<div class="tf-none">No values to filter on.</div>'
          }</div>`;
      } else if (col.filter === 'number') {
        bodyHtmlStr = `
          <div class="tf-range">
            <input type="number" class="tf-num" data-min placeholder="min" value="${esc((f && f.min) || '')}"/>
            <span class="tf-dash">–</span>
            <input type="number" class="tf-num" data-max placeholder="max" value="${esc((f && f.max) || '')}"/>
          </div>`;
      } else {
        const op = (f && f.op) || 'contains';
        bodyHtmlStr = `
          <select class="tf-op">${TEXT_OPS.map(([k, l]) =>
            `<option value="${k}" ${op === k ? 'selected' : ''}>${esc(l)}</option>`).join('')}</select>
          <input type="text" class="tf-v" placeholder="value" autocomplete="off" spellcheck="false"
                 value="${esc((f && f.v) || '')}" ${opNeedsValue(op) ? '' : 'disabled'}/>`;
      }

      return `
        <div class="tf-head">${esc(col.label || col.key)}</div>
        ${sortRow}
        <div class="tf-body">${bodyHtmlStr}</div>
        <div class="tf-foot">
          <button type="button" class="tf-clear" data-clear ${f ? '' : 'disabled'}>Clear filter</button>
          <button type="button" class="tf-done btn-primary btn-sm" data-done>Done</button>
        </div>`;
    }

    function wirePanel(col, anchor) {
      const pop = popRoot();

      // Chosen from the panel, sorting is a single key: the operator picked a direction for THIS
      // column, not a place in a stack. Shift-clicking headers is how a stack is built.
      pop.querySelectorAll('[data-sort]').forEach(b => b.addEventListener('click', () => {
        if (b.dataset.sort === 'none') {
          const at = view.sort.findIndex(s => s.key === col.key);
          if (at !== -1) view.sort.splice(at, 1);
        } else {
          view.sort = [{ key: col.key, dir: b.dataset.sort }];
        }
        persist();
        paint();
        closePop();
      }));

      pop.querySelector('[data-clear]')?.addEventListener('click', () => {
        setFilter(col.key, null);
        closePop();
        anchor.focus();
      });
      pop.querySelector('[data-done]')?.addEventListener('click', () => { closePop(); anchor.focus(); });

      if (col.filter === 'enum') {
        const boxes = () => Array.from(pop.querySelectorAll('.tf-list input[data-val]'));
        const shownBoxes = () => boxes().filter(b => b.closest('.tf-opt').style.display !== 'none');

        const apply = () => {
          const sel = boxes().filter(b => b.checked).map(b => b.dataset.val);
          const all = sel.length === boxes().length;
          // Everything ticked is the same view as nothing ticked, and storing it as a filter would
          // leave a chip that narrows nothing.
          setFilter(col.key, all ? null : normalizeFilter(col.key, { type: 'enum', sel }));
          const allBox = pop.querySelector('[data-all]');
          if (allBox) allBox.checked = all;
          const clr = pop.querySelector('[data-clear]');
          if (clr) clr.disabled = !view.filters[col.key];
        };

        boxes().forEach(b => b.addEventListener('change', apply));

        pop.querySelector('[data-all]')?.addEventListener('change', (e) => {
          shownBoxes().forEach(b => { b.checked = e.target.checked; });
          apply();
        });

        pop.querySelector('.tf-find')?.addEventListener('input', (e) => {
          const q = e.target.value.trim().toLowerCase();
          pop.querySelectorAll('.tf-list .tf-opt').forEach(o => {
            o.style.display = (!q || (o.dataset.v || '').indexOf(q) !== -1) ? '' : 'none';
          });
        });
      } else if (col.filter === 'number') {
        const read = () => ({
          type: 'number',
          min: pop.querySelector('[data-min]').value.trim(),
          max: pop.querySelector('[data-max]').value.trim(),
        });
        const apply = () => {
          setFilter(col.key, normalizeFilter(col.key, read()));
          const clr = pop.querySelector('[data-clear]');
          if (clr) clr.disabled = !view.filters[col.key];
        };
        pop.querySelectorAll('.tf-num').forEach(i => {
          i.addEventListener('input', debounce(apply, 200));
          i.addEventListener('keydown', (e) => { if (e.key === 'Enter') { apply(); closePop(); } });
        });
      } else {
        const opSel = pop.querySelector('.tf-op');
        const valIn = pop.querySelector('.tf-v');
        const apply = () => {
          const f = { type: 'text', op: opSel.value, v: valIn.value };
          setFilter(col.key, normalizeFilter(col.key, f));
          const clr = pop.querySelector('[data-clear]');
          if (clr) clr.disabled = !view.filters[col.key];
        };
        opSel.addEventListener('change', () => {
          valIn.disabled = !opNeedsValue(opSel.value);
          if (valIn.disabled) valIn.value = '';
          apply();
          if (!valIn.disabled) valIn.focus();
        });
        valIn.addEventListener('input', debounce(apply, 200));
        valIn.addEventListener('keydown', (e) => { if (e.key === 'Enter') { apply(); closePop(); } });
      }
    }

    // ── Mount ─────────────────────────────────────────────────────────────────
    // Tabs rebuild #contentBody wholesale on every save, so this runs often; the view lives on the
    // instance (and in sessionStorage), never in the DOM it is rebuilding.
    function mount(container, rows) {
      host = container;
      if (!host) return;
      setRows(rows, true);

      host.className = 'tbl-host ' + (spec.hostClass || '');
      host.innerHTML = barHtml() +
        `<table class="cfg-table tbl ${esc(spec.tableClass || '')}">
           <thead></thead><tbody></tbody>
         </table>`;

      const q = host.querySelector('.tbl-q');
      q.addEventListener('input', debounce(() => { view.q = q.value; persist(); paint(); }, 140));
      // A search that narrowed to nothing is the one place Escape should empty the box rather than
      // leave the operator deleting characters.
      q.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && q.value) { e.preventDefault(); q.value = ''; view.q = ''; persist(); paint(); }
      });

      host.addEventListener('click', (e) => {
        if (e.target.closest('[data-reset]')) { clearAll(); return; }

        const funnel = e.target.closest('.th-filter');
        if (funnel) { e.preventDefault(); e.stopPropagation(); openFilter(funnel.dataset.filter, funnel); return; }

        const chipSort = e.target.closest('[data-unsort]');
        if (chipSort) {
          const at = view.sort.findIndex(s => s.key === chipSort.dataset.unsort);
          if (at !== -1) { view.sort.splice(at, 1); persist(); paint(); }
          return;
        }
        const chipFilter = e.target.closest('[data-unfilter]');
        if (chipFilter) { setFilter(chipFilter.dataset.unfilter, null); return; }

        const th = e.target.closest('th.th-sortable');
        if (th) toggleSort(th.dataset.k, e.shiftKey);
      });

      host.addEventListener('keydown', (e) => {
        if (e.key !== 'Enter' && e.key !== ' ') return;
        const chip = e.target.closest('[data-unsort],[data-unfilter]');
        if (chip) { e.preventDefault(); chip.click(); return; }
        const th = e.target.closest('th.th-sortable');
        if (th && e.target === th) { e.preventDefault(); toggleSort(th.dataset.k, e.shiftKey); }
      });

      paint();
    }

    function setRows(rows, quiet) {
      entries = (rows || []).map((row, i) => ({ row, i, t: {} }));
      if (!quiet) paint();
    }

    return { mount, setRows, paint, clear: clearAll };
  }

  function debounce(fn, ms) {
    let t = null;
    return function () {
      clearTimeout(t);
      t = setTimeout(fn, ms);
    };
  }

  // Configuration switches tabs without a page load, and the panel is mounted on <body> — so it
  // would otherwise survive the switch, floating over a table it does not belong to.
  document.addEventListener('nms:tab-change', closePop);

  window.NMS.table = { create };
})();
