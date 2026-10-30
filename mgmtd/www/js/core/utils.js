/* utils.js — the helpers every page draws on.
 *
 *   NMS.utils.esc / newUuid / fetchJSON / loadSettings / siteScope / enhanceSelect(s) / icons
 *   NMS.navBadge  the counts the sidebar carries
 *
 * Loaded before anything else that is not the session guard: the tab modules destructure from
 * NMS.utils at module scope, so it has to exist by the time they run.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  window.NMS.navBadge = {
    set(id, n) {
      const v = Math.max(0, n | 0);
      try {
        if (v > 0) sessionStorage.setItem(NAV_BADGE_KEY + id, String(v));
        else sessionStorage.removeItem(NAV_BADGE_KEY + id);
      } catch (_) { /* quota/private mode — the badge just does not persist */ }
      this.paint(id);
    },
    get(id) {
      try { return parseInt(sessionStorage.getItem(NAV_BADGE_KEY + id), 10) || 0; } catch (_) { return 0; }
    },
    // Empty rather than "0": `.nav-badge:empty` is display:none, so a count of nothing leaves the
    // row exactly as it would look without a badge at all.
    paint(id) {
      const el = document.getElementById(id);
      if (!el) return;
      const n = this.get(id);
      el.textContent = n > 0 ? String(n) : '';
    },
    paintAll() {
      document.querySelectorAll('.nav-badge[id]').forEach(el => this.paint(el.id));
    },
  };


  window.NMS.utils = {
    // The Operation cards' icon set. Read it as `window.NMS.utils.icons.view`; a card that needs a
    // glyph nobody else has adds it here rather than starting a fourth private copy.
    icons: OP_ICONS,

    // Shared by the settings tab modules (see www/js/sites.js and friends).
    esc(s) {
      return String(s == null ? '' : s).replace(/[&<>"]/g, c =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    },
    newUuid() {
      if (crypto && crypto.randomUUID) return crypto.randomUUID();
      return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
        const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 0x3 | 0x8)).toString(16);
      });
    },
    formatUptime(seconds) {
      if (!seconds || seconds < 0) return '--';
      const d = Math.floor(seconds / 86400);
      const h = Math.floor((seconds % 86400) / 3600);
      const m = Math.floor((seconds % 3600) / 60);
      const s = seconds % 60;
      if (d) return `${d}d ${h}h ${m}m`;
      if (h) return `${h}h ${m}m ${s}s`;
      return `${m}m ${s}s`;
    },
    // Dispatch-and-poll, the shape every mgmtd call that crosses to pretzel-ai has: the POST/GET
    // returns 202 {ticket} immediately and the answer is collected from a result endpoint. mgmtd
    // runs one event loop, so a call that blocked it for the length of a gRPC round trip would
    // freeze the console — the ticket is what keeps that thread free.
    //
    // Resolves with the answer document. Rejects on transport failure or when the answer never
    // arrives; `tries` × `everyMs` is the ceiling, and it is deliberately generous because the
    // work on the far side is a database read whose tail is a cold connection, not a crawl.
    async pollTicket(dispatchUrl, resultUrl, opts) {
      const o = Object.assign({ everyMs: 300, tries: 60 }, opts || {});
      const first = await this.fetchJSON(dispatchUrl, o.dispatch);
      if (!first) return null;                       // 401: fetchJSON already redirected
      if (first.error) return first;                 // refused before dispatch; it is the answer
      const ticket = first.ticket;
      if (!ticket) throw new Error('no ticket');

      const url = resultUrl + (resultUrl.indexOf('?') === -1 ? '?' : '&') + 'ticket=' + ticket;
      for (let i = 0; i < o.tries; i++) {
        await new Promise(r => setTimeout(r, o.everyMs));
        const got = await this.fetchJSON(url);
        if (!got) return null;
        if (got.status !== 'pending') return got;
      }
      throw new Error('timed out waiting for an answer');
    },

    // The connector-test shape, which four pages were each writing out: POST the (possibly
    // uncommitted) target to mgmtd, get a ticket back, poll one fixed result endpoint until the
    // answer is done. The browser cannot reach a customer's firewall, so every one of these is a
    // round trip through collectord and the wait is measured in seconds, not milliseconds.
    //
    // Three things the copies did not agree on, settled here:
    //   · 401. Only one of the four redirected to login, so a test started after the session
    //     expired failed with a parse error on the login page instead of a sign-in prompt.
    //   · 404. Worth its own message — it means the build is older than the page, not that the
    //     device is unreachable.
    //   · The first poll waits before asking. A device exchange has never finished in under a
    //     tick, and asking immediately only costs a round trip.
    // Every settings editor opens the same way: read the whole configuration document, tell the
    // draft layer which version this page was built against, and take its own slice. Eight pages
    // were spelling that out line for line — including the version check, which is what makes the
    // draft refuse to publish on top of a configuration that moved underneath it.
    //
    // Answers null when the session has expired (the page is already navigating to login), so the
    // caller's whole error path is `if (!d) return;`.
    async loadSettings() {
      const d = await this.fetchJSON('/api/settings', { headers: { Accept: 'application/json' } });
      if (!d) return null;
      // Optional on purpose: this helper is core and loads on every page, while the draft store
      // is the staging layer and loads only where a candidate can exist. Calling it unguarded
      // made a file every page depends on depend back on one most pages do not have — true today
      // only because nothing outside Configuration happens to call loadSettings().
      if (window.NMS.draft) window.NMS.draft.checkBase(d.version);

      // The sidebar's switcher is fed from a cache, and until now only Topology and API Collection
      // refilled it — so a site added under Configuration ▸ Sites did not appear in the switcher
      // until the operator happened to visit one of those two pages. This document already carries
      // the list, and every settings editor reads it, so the one place a site can be created is now
      // also a place the switcher hears about it.
      const sites = (((d.scopes || {}).pretzel || {}).site || {}).sites;
      if (Array.isArray(sites)) this.siteScope.publish(sites);

      return d;
    },

    async runConnectorTest(dispatchUrl, payload, opts) {
      const o = Object.assign(
        { everyMs: 700, tries: 40, resultUrl: '/api/connector/test-result', startError: '', timeoutError: '' },
        opts || {});

      const start = await fetch(dispatchUrl, {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      // Unlike fetchJSON, this answers with a shaped result rather than null: the page is on its
      // way to the login screen either way, and a null would throw out of the caller's first
      // property read before the navigation lands.
      if (start.status === 401) { window.location.href = '/'; return { ok: false, message: 'session expired' }; }
      if (start.status === 404) throw new Error('backend test endpoint is not available');

      const started = await start.json().catch(() => null);
      if (!start.ok || !started || !started.ticket)
        throw new Error((started && started.error) || o.startError || ('HTTP ' + start.status));

      const url = o.resultUrl + (o.resultUrl.indexOf('?') === -1 ? '?' : '&') + 'ticket=' + started.ticket;
      for (let i = 0; i < o.tries; i++) {
        await new Promise(r => setTimeout(r, o.everyMs));
        const r = await fetch(url, { credentials: 'same-origin', headers: { Accept: 'application/json' } });
        if (r.status === 401) { window.location.href = '/'; return { ok: false, message: 'session expired' }; }
        const d = await r.json().catch(() => null);
        if (d && d.status === 'done') return d;
      }
      throw new Error(o.timeoutError || 'timed out waiting for the device');
    },

    async fetchJSON(url, opts) {
      const r = await fetch(url, Object.assign({ credentials: 'same-origin' }, opts));
      if (r.status === 401) { window.location.href = '/'; return null; }
      if (!r.ok) throw new Error(r.status);
      return r.json();
    },

    // Every timestamp mgmtd emits is formatted by Postgres `OF`, which prints a whole-hour zone as
    // `+09` — not the `+09:00` ISO 8601 requires, so `new Date()` rejects it outright and every
    // arithmetic on the result silently becomes NaN. Pad the offset back to two fields before
    // parsing. Returns null (never an Invalid Date) when the value cannot be used.
    parseTs(s) {
      if (!s) return null;
      const t = new Date(String(s).replace(/([+-]\d{2})$/, '$1:00'));
      return isNaN(t.getTime()) ? null : t;
    },

    // An absolute timestamp, always 24-hour and always the same shape: YYYY-MM-DD HH:MM:SS in the
    // viewer's local time. Not toLocaleString(), which follows the browser's locale — on a Korean
    // browser that renders "2026. 8. 4. 오후 3:39:35", so the same appliance reads differently to
    // two operators and half the log lines it is compared against are 24-hour anyway.
    // Accepts a Date, an epoch-milliseconds number, or a server timestamp string (see parseTs).
    fmtTs(v) {
      const t = (v instanceof Date) ? v : (typeof v === 'number' ? new Date(v) : this.parseTs(v));
      if (!t || isNaN(t.getTime())) return '—';
      const p = (n) => String(n).padStart(2, '0');
      return `${t.getFullYear()}-${p(t.getMonth() + 1)}-${p(t.getDate())} ` +
             `${p(t.getHours())}:${p(t.getMinutes())}:${p(t.getSeconds())}`;
    },

    // The site the operator is working in, shared by every Insight page and remembered across
    // visits. A scope is a property of the session, not of one page: picking a site on Infrastructure
    // and being dropped back at Overview on API Collection is the same annoyance as a console that
    // forgets your region. Datadog, Grafana and the AWS console all keep this.
    //
    // Reading it is deliberately forgiving — a site can be deleted between visits, so callers check
    // the value against the sites they actually received and fall back to Overview.
    siteScope: {
      key: 'pz.site',
      listKey: 'pz.sites',

      get() {
        try { return localStorage.getItem(this.key) || ''; } catch (_) { return ''; }
      },

      // Writes through to the sidebar control and to whatever page is open. The control lives in
      // the shell now, so a page can no longer be the only thing that knows the scope changed.
      set(oid, opts) {
        const next = oid || '';
        if (next === this.get()) return;
        try {
          if (next) localStorage.setItem(this.key, next);
          else localStorage.removeItem(this.key);
        } catch (_) { /* private mode — the scope just does not persist */ }

        this._subs.forEach(fn => { try { fn(next); } catch (_) { /* one bad listener is not the rest */ } });
        if (!opts || !opts.silent) window.NMS.utils.syncSidebarScope?.();
      },

      // A page calls this to be told when the scope changes from the sidebar. Returns an unsubscribe.
      subscribe(fn) {
        this._subs.push(fn);
        return () => { this._subs = this._subs.filter(f => f !== fn); };
      },
      _subs: [],

      // The sites the switcher offers. Cached in localStorage so the control is populated on the
      // first paint of every page, including the ones that never load a site list of their own —
      // the sidebar is global and a switcher that is empty until a fetch lands reads as broken.
      options() {
        try { return JSON.parse(localStorage.getItem(this.listKey) || '[]') || []; } catch (_) { return []; }
      },

      // Pages that already hold the list publish it here for free. Shape: [{ oid, name }].
      publish(list) {
        const clean = (Array.isArray(list) ? list : [])
          .filter(x => x && x.oid)
          .map(x => ({ oid: String(x.oid), name: String(x.name || x.oid) }))
          .sort((a, b) => a.name.localeCompare(b.name));

        // Guarded on the serialised value: pages publish on every poll, and rebuilding the control
        // a minute would tear down a panel the operator has open. The site list moves on a config
        // commit, not on a refresh.
        const next = JSON.stringify(clean);
        let prev = null;
        try { prev = localStorage.getItem(this.listKey); } catch (_) { /* no cache */ }
        if (prev === next) return clean;

        try { localStorage.setItem(this.listKey, next); } catch (_) { /* no cache */ }
        window.NMS.utils.syncSidebarScope?.();
        return clean;
      },

    },

    // "just now" / "42s" / "7m" / "3h" / "2d" — the age of a timestamp, for a freshness stamp.
    //
    // Each unit hands over at exactly one of the next, not at one and a half. It used to run
    // minutes to 90 and hours to 48, which produced "70m ago" and "36h ago" — readings this
    // function's own examples never contemplated, and that a reader has to divide before they mean
    // anything. Precision that costs arithmetic is not precision.
    //
    // Floored, not rounded, because that is what "ago" means: a thing 1h59m old happened "1h ago",
    // not "2h ago" — it has not been two hours. Rounding also pushed the boundary values up a unit
    // the moment they arrived there.
    relAge(s) {
      const t = this.parseTs(s);
      if (!t) return '—';
      const sec = Math.max(0, (Date.now() - t.getTime()) / 1000);
      if (sec < 5) return 'just now';
      if (sec < 60) return Math.floor(sec) + 's ago';
      if (sec < 3600) return Math.floor(sec / 60) + 'm ago';
      if (sec < 86400) return Math.floor(sec / 3600) + 'h ago';
      return Math.floor(sec / 86400) + 'd ago';
    },

    // ── Invalid fields ────────────────────────────────────────────────────────────────────────
    //
    // An editor that refuses to save says why in one line at the foot of the panel. That line is
    // easy to miss and, in a form of eight fields, it does not say WHICH one — so the operator
    // reads the sentence, looks back at the form, and has to work it out. These mark the field.
    //
    // Orange rather than red: nothing is broken and nothing was lost. The panel is still open,
    // the value is still there, and the operator is being asked to change it — which is the same
    // thing every other orange on this console means (a checkpoint switched off, a key staged, a
    // guardrail that will fail open).
    //
    // `clearInvalid` runs first on every validation pass, so a field that was flagged and then
    // corrected stops being flagged without each caller having to remember.
    clearInvalid(scope) {
      if (scope) scope.querySelectorAll('.is-invalid').forEach(el => el.classList.remove('is-invalid'));
    },

    // `names` are data-f / data-pw style attribute values, or any selector. Returns false so a
    // caller can `return markInvalid(...)` from a validation branch and keep it to one line.
    markInvalid(scope, names) {
      this.clearInvalid(scope);
      if (!scope) return false;
      (Array.isArray(names) ? names : [names]).forEach(n => {
        // A bare name is expanded into the attribute forms the editors use; anything carrying
        // selector punctuation is taken as written, so a page with its own convention can pass one.
        const isSelector = n.includes('[') || n.includes('.') || n.includes('#') || n.includes(' ');
        const sel = isSelector ? n : `[data-f="${n}"], [data-pw="${n}"], #${n}`;
        scope.querySelectorAll(sel).forEach(el => el.classList.add('is-invalid'));
      });
      // The first one is what the operator should be looking at.
      scope.querySelector('.is-invalid')?.focus?.();
      return false;
    },

    // One editor refusal: say what is wrong at the top of the panel, and mark the field it is
    // about. Replaces window.alert, which was drawn by the browser in the operating system's
    // typography, said nothing about WHICH field, and had to be dismissed before the operator
    // could look at the form again.
    //
    // The note is injected rather than expected in the markup so every editor gets it without
    // each one growing a slot for it. Returns false, so a validation branch is one line:
    //   if (!d.name) return NMS.utils.editorError(body, 'Name is required.', 'name');
    editorError(scope, note, field) {
      if (!scope) return false;
      let el = scope.querySelector('.ed-error');
      if (!el) {
        el = document.createElement('div');
        el.className = 'ed-error';
        scope.insertBefore(el, scope.firstChild);
      }
      el.textContent = note;
      if (field) this.markInvalid(scope, field);
      else this.clearInvalid(scope);
      return false;
    },

    // Called when a save is attempted, before the checks: a message left over from the previous
    // attempt is a message about a field the operator may already have fixed.
    clearEditorError(scope) {
      if (!scope) return;
      scope.querySelector('.ed-error')?.remove();
      this.clearInvalid(scope);
    },
    // Build (or rebuild) the sidebar's site switcher from the cached list and the current scope.
    // Idempotent: called on first paint, whenever a page publishes a fresher list, and after the
    // scope changes from anywhere.
    syncSidebarScope() {
      const host = document.getElementById('navSite');
      if (!host) return;

      const scope = this.siteScope;
      const sites = scope.options();
      let cur = scope.get();

      // A remembered site can be deleted between visits. The pointer is then stale, not a choice —
      // drop it and let the operator pick again rather than showing a name the estate does not have.
      if (cur && sites.length && !sites.some(x => x.oid === cur)) {
        scope.set('', { silent: true });
        cur = '';
      }
      // A one-site estate has exactly one answer, and making the operator choose it is ceremony.
      if (!cur && sites.length === 1) {
        scope.set(sites[0].oid, { silent: true });
        cur = sites[0].oid;
      }

      const chosen = sites.find(x => x.oid === cur);
      const label = chosen ? chosen.name : (sites.length ? 'Select a site' : 'No sites');

      // Same shape Configuration's group uses, so it inherits the flyout wholesale — hover to open,
      // anchored right, dismissed on scroll. `site` marks an entry as a scope change rather than a
      // destination; initFlyouts reads it the way it reads data-goto-tab.
      // The oid beside each name, because two sites can be called the same thing and the name is
      // the only other thing on the row. It is also what every reference in the configuration and
      // every row in the database is keyed by, so it is the value an operator ends up needing when
      // a site has to be matched against something outside this screen.
      const subs = sites.map(x => ({
        // First eight characters: enough to tell two sites apart at a glance and to match against a
        // log line, without a 36-character column deciding how wide the panel is. The whole value is
        // on the row's title for the times it has to be copied.
        id: x.oid, label: x.name, href: '#', site: x.oid,
        note: String(x.oid).slice(0, 8), title: x.oid,
        active: x.oid === cur,
      }));
      if (!sites.length) {
        subs.push({ id: '', label: 'Add a site in Configuration \u25b8 Sites',
                    href: 'settings?tab=sites', note: '', active: false });
      }

      const data = JSON.stringify(subs).replace(/&/g, '&amp;').replace(/"/g, '&quot;');
      host.innerHTML =
        `<button type="button" class="nav-group-toggle nav-site-toggle${chosen ? '' : ' is-unset'}"
                 data-tooltip="${this.esc(label)}" data-label="Site" data-subitems="${data}"
                 aria-haspopup="true" aria-expanded="false">
           <svg class="nav-site-ic" width="17" height="17" viewBox="0 0 24 24" fill="none"
                stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"
                aria-hidden="true">
             <path d="M12 21s7-5.686 7-11a7 7 0 1 0-14 0c0 5.314 7 11 7 11z"/>
             <circle cx="12" cy="10" r="2.6"/>
           </svg>
           <span class="nav-site-nm">${this.esc(label)}</span>
           <svg class="nav-site-caret" width="12" height="12" viewBox="0 0 24 24" fill="none"
                stroke="currentColor" stroke-width="2.4" stroke-linecap="round">
             <polyline points="9 6 15 12 9 18"/>
           </svg>
         </button>`;

      this.syncScopeGating();
    },

    // The links this scope governs are unusable without it, so they say so rather than opening on
    // a page that can only apologise. Marked rather than hidden: a section that disappears reads as
    // a feature this build does not have.
    syncScopeGating() {
      const chosen = !!this.siteScope.get();
      // The rows slide away rather than sitting there greyed out: with no site there is nothing
      // behind them to show, and a list of four dead links is a worse answer than no list. The fold
      // is one decision made in one place — tierOpenState — because the operator's own fold has a
      // say in it too.
      window.NMS.syncTiers?.();
      document.querySelectorAll('.nav-item[data-scoped="1"]').forEach((el) => {
        el.classList.toggle('nav-disabled', !chosen);
        if (chosen) {
          el.setAttribute('href', el.dataset.page || '#');
          el.setAttribute('data-tooltip', el.dataset.label || '');
        } else {
          el.setAttribute('href', '#');
          el.setAttribute('data-tooltip', (el.dataset.label || '') + ' (select a site first)');
        }
      });
    },
    enhanceSelects(container) {
      if (container) container.querySelectorAll('select').forEach(s => this.enhanceSelect(s));
    },

    // Replace a native <select>'s look with a themed dropdown that renders identically on Windows,
    // macOS and Linux — the OS draws native option lists and CSS cannot touch them. The <select>
    // stays in the DOM (hidden) as the value store and event source, so existing `.value` reads and
    // 'change' listeners keep working; picking an option sets it and dispatches a bubbling 'change'.
    enhanceSelect(select) {
      if (!select || select.dataset.csEnhanced) return;
      select.dataset.csEnhanced = '1';

      const wrap = document.createElement('div');
      wrap.className = 'cs';
      select.parentNode.insertBefore(wrap, select);
      wrap.appendChild(select);

      const trigger = document.createElement('button');
      trigger.type = 'button';
      trigger.className = 'cs-trigger';
      trigger.setAttribute('aria-haspopup', 'listbox');
      trigger.setAttribute('aria-expanded', 'false');
      trigger.innerHTML = '<span class="cs-label"></span><span class="cs-caret" aria-hidden="true"></span>';
      wrap.appendChild(trigger);
      const label = trigger.querySelector('.cs-label');

      let panel = null, active = -1;

      const syncLabel = () => {
        const o = select.options[select.selectedIndex];
        label.textContent = o ? o.textContent : '';
        label.classList.toggle('cs-placeholder', !o || o.value === '');
      };

      const optionEls = () => (panel ? Array.from(panel.querySelectorAll('.cs-opt:not(.disabled)')) : []);

      const setActive = (i) => {
        const opts = optionEls();
        if (!opts.length) return;
        active = (i + opts.length) % opts.length;
        opts.forEach((el, idx) => el.classList.toggle('active', idx === active));
        opts[active].scrollIntoView({ block: 'nearest' });
      };

      const position = () => {
        const r = trigger.getBoundingClientRect();
        panel.style.width = r.width + 'px';
        panel.style.left = r.left + 'px';
        const below = window.innerHeight - r.bottom;
        const ph = panel.offsetHeight;
        panel.style.top = (below < ph + 8 && r.top > below ? r.top - ph - 6 : r.bottom + 6) + 'px';
      };

      const close = () => {
        if (!panel) return;
        panel.remove(); panel = null; active = -1;
        trigger.setAttribute('aria-expanded', 'false');
        document.removeEventListener('mousedown', onDocDown, true);
        document.removeEventListener('keydown', onKey, true);
        window.removeEventListener('resize', close, true);
        window.removeEventListener('scroll', onScroll, true);
        if (csActiveClose === close) csActiveClose = null;
      };

      const choose = (optionIndex) => {
        if (select.selectedIndex !== optionIndex) {
          select.selectedIndex = optionIndex;
          select.dispatchEvent(new Event('change', { bubbles: true }));
        }
        syncLabel();
        close();
        trigger.focus();
      };

      const onDocDown = (e) => {
        if (panel && !panel.contains(e.target) && !wrap.contains(e.target)) close();
      };

      // The panel is positioned once, against the trigger's box, so a page that scrolls underneath
      // it leaves it floating somewhere it no longer belongs — hence closing on scroll. But the
      // listener is on `window` in the CAPTURE phase, which sees scrolls of every element on the
      // way down, including the panel's own: a list long enough to need a scrollbar closed itself
      // the moment the operator dragged it. Scrolls that start inside the panel are its business,
      // not the page's, so they are let through.
      const onScroll = (e) => {
        if (panel && e.target instanceof Node && panel.contains(e.target)) return;
        close();
      };

      const onKey = (e) => {
        if (!panel) return;
        if (e.key === 'Escape') { e.preventDefault(); close(); trigger.focus(); }
        else if (e.key === 'ArrowDown') { e.preventDefault(); setActive(active + 1); }
        else if (e.key === 'ArrowUp') { e.preventDefault(); setActive(active - 1); }
        else if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          const opts = optionEls();
          if (active >= 0 && opts[active]) opts[active].click();
        }
      };

      const open = () => {
        if (panel) { close(); return; }
        if (csActiveClose) csActiveClose();

        panel = document.createElement('div');
        panel.className = 'cs-panel';
        panel.setAttribute('role', 'listbox');
        Array.from(select.options).forEach((o, i) => {
          const el = document.createElement('div');
          el.className = 'cs-opt'
            + (o.disabled ? ' disabled' : '')
            + (i === select.selectedIndex ? ' sel' : '')
            + (o.value === '' ? ' placeholder' : '');
          el.setAttribute('role', 'option');
          el.textContent = o.textContent;
          if (!o.disabled) el.addEventListener('click', () => choose(i));
          panel.appendChild(el);
        });
        document.body.appendChild(panel);
        trigger.setAttribute('aria-expanded', 'true');
        position();

        const opts = optionEls();
        const sel = opts.findIndex(el => el.classList.contains('sel'));
        setActive(sel >= 0 ? sel : 0);

        document.addEventListener('mousedown', onDocDown, true);
        document.addEventListener('keydown', onKey, true);
        window.addEventListener('resize', close, true);
        window.addEventListener('scroll', onScroll, true);
        csActiveClose = close;
      };

      trigger.addEventListener('click', open);
      select.addEventListener('change', syncLabel);
      syncLabel();
    },
  };
})();
