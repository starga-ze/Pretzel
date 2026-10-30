/* topbar.js — the band above the page: its title, its tabs, and the Config Review button.
 *
 * The button is the whole reason this is its own module rather than part of nav: it is the one
 * control on the page whose state comes from somewhere else entirely — the staging registry,
 * which only exists on Configuration. Everything here tolerates its absence.
 *
 *   NMS.topbar.build()
 *   NMS.setPendingChanges(n)   called by the staging registry; paints the button
 *   NMS.onCommit(fn)           what the button opens
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // The console's shape is nav's to define; this module only draws the band at the top
  // of it. Aliased at load rather than reached through on every use, so the bodies below
  // read exactly as they did when they shared a file with it.
  const { PAGES, SETTINGS_GROUPS, groupOfTab } = window.NMS.nav;
  const syncSidebarActive = window.NMS.nav.syncActive;

  function buildTopbar() {
    const el = document.getElementById('pageTopbar');
    if (!el) return;

    const page = location.pathname.split('/').pop() || 'home';
    const cfg = PAGES[page];
    if (!cfg) { el.remove(); return; }

    // Grouped pages still address a tab with a single ?tab= id; the group is derived from it, so
    // links stay short and a module only ever has to look at one parameter.
    const allTabs = cfg.groups ? cfg.groups.reduce((acc, g) => acc.concat(g.tabs), []) : (cfg.tabs || []);
    const defaultTab = allTabs.length ? allTabs[0].id : '';
    const params = new URLSearchParams(location.search);
    const requested = params.get('tab');
    const activeTab = allTabs.some(t => t.id === requested) ? requested : defaultTab;
    const activeGroup = cfg.groups
      ? (cfg.groups.find(g => g.tabs.some(t => t.id === activeTab)) || cfg.groups[0])
      : null;

    // With the group chosen in the sidebar, the heading names the group — otherwise the flyout
    // closes and nothing on screen says which part of Configuration this is.
    const heading = activeGroup ? activeGroup.label : cfg.title;
    document.title = 'Pretzel — ' + heading;

    // The whole band, not just its buttons. A chat surface wants the height more than it wants to
    // be told its own name, and the sidebar already says which page this is.
    if (PAGES[page] && PAGES[page].bareTopbar) { el.remove(); return; }

    // Publish and Revert are the two ends of the candidate: send it, or throw it away. Only the
    // pages that HAVE a candidate carry them, which today means Configuration minus System
    // Operation (see SETTINGS_GROUPS). Everywhere else they were chrome — a button that could
    // only ever be pressed to be told to go somewhere else, and an always-disabled Publish is
    // what teaches an operator to stop reading the button, which is the one thing it must never
    // become. Refresh is not part of that pair and stays: every page re-reads.
    //
    // The cost is that a page outside Configuration no longer shows that changes are staged. That
    // is the honest trade — the button was never actionable there — and the staged state still
    // survives the walk, because it lives in sessionStorage and not in the button.
    const stages = !!cfg.staging && (!activeGroup || activeGroup.staging !== false);

    // One control and one click. It was briefly a menu offering Publish and Revert, which asked the
    // operator to choose between them before being shown anything — and the answer to "publish or
    // discard" is in the diff, not in a menu. The dialog carries both: Publish on the right, where
    // decisions are confirmed, and Discard on the left, where a destructive option belongs when it
    // belongs anywhere. So the button says what it opens and opens it.
    const stagingButtons = `
          <button class="topbar-review-btn" id="reviewBtn" type="button" disabled
                  title="Review the staged configuration">
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
              <polyline points="14 2 14 8 20 8"/>
            </svg>
            <span>Config Review</span>
            <span class="topbar-review-count" id="reviewCount" hidden>0</span>
          </button>`;

    let html = `
      <div class="topbar-main">
        <h1 class="topbar-title">${heading}</h1>
        <div class="topbar-actions">${stages ? stagingButtons : ''}
          <button class="btn btn-ghost btn-icon-only" id="refreshBtn" type="button" title="Refresh" aria-label="Refresh">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
              <polyline points="23 4 23 10 17 10"/>
              <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
            </svg>
          </button>
        </div>
      </div>`;

    const tabRow = (tabs) => `<nav class="page-tabs">` + tabs.map(t =>
      `<a class="page-tab ${t.id === activeTab ? 'active' : ''}" href="${page}?tab=${t.id}" data-tab="${t.id}">${t.label}</a>`
    ).join('') + `</nav>`;

    if (cfg.groups) {
      // One row only: the tabs of the group the sidebar opened. A single-tab group is its own
      // content, so a tab row of one would be noise.
      if (activeGroup.tabs.length > 1) {
        el.classList.add('has-tabs');
        html += tabRow(activeGroup.tabs);
      }
    } else if (cfg.tabs) {
      el.classList.add('has-tabs');
      html += tabRow(cfg.tabs);
    }

    el.innerHTML = html;

    // Settings navigation is client-side: every tab module stays loaded with its in-memory and
    // staged state, so the topbar — and the Publish pending state — survives the switch
    // instead of being rebuilt disabled and re-enabled once the data refetch lands. Modules
    // repaint on 'nms:tab-change'. Other tabbed pages (audit) keep plain navigation.
    if (cfg.groups && page === 'settings') {
      const currentTab = () => {
        const t = new URLSearchParams(location.search).get('tab');
        return allTabs.some(x => x.id === t) ? t : defaultTab;
      };

      function onTabClick(e) {
        e.preventDefault();
        const tabId = e.currentTarget.dataset.tab;
        if (tabId !== currentTab()) switchTab(tabId, true);
      }

      function switchTab(tabId, push) {
        if (push) history.pushState({ tab: tabId }, '', `${page}?tab=${tabId}`);

        const group = groupOfTab(tabId);

        // Changing group changes both the heading and which tabs exist.
        const title = el.querySelector('.topbar-title');
        if (title) title.textContent = group.label;
        document.title = 'Pretzel — ' + group.label;

        const existing = el.querySelector('.page-tabs');
        if (group.tabs.length > 1) {
          if (existing) existing.outerHTML = tabRow(group.tabs);
          else el.insertAdjacentHTML('beforeend', tabRow(group.tabs));
          el.classList.add('has-tabs');
          el.querySelectorAll('.page-tab').forEach(a => {
            a.classList.toggle('active', a.dataset.tab === tabId);
            a.addEventListener('click', onTabClick);
          });
        } else {
          if (existing) existing.remove();
          el.classList.remove('has-tabs');
        }

        // Keep the sidebar highlight in step. Only the stored subitem state and the links'
        // classes are rewritten — rebuilding the sidebar would mean re-running initFlyouts, whose
        // document/window listeners would then accumulate on every switch.
        syncSidebarActive(tabId);

        document.dispatchEvent(new CustomEvent('nms:tab-change', { detail: { tab: tabId, group: group.id } }));
      }

      el.querySelectorAll('.page-tab').forEach(a => a.addEventListener('click', onTabClick));
      window.addEventListener('popstate', () => switchTab(currentTab(), false));

      // The sidebar flyout navigates between groups. While already on this page that should be
      // the same in-place switch as a tab click — a full load would rebuild the topbar and make
      // the Publish state flicker back through disabled.
      window.NMS = window.NMS || {};
      window.NMS.gotoSettingsTab = (tabId) => {
        if (!allTabs.some(t => t.id === tabId) || tabId === currentTab()) return false;
        switchTab(tabId, true);
        return true;
      };
    }

    // Nothing paints the button here. It ships disabled in its own markup and the staging
    // registry (js/commit.js) turns it on once it can actually answer — which is the only moment
    // anything knows whether there is a candidate.
    //
    // It used to be pre-painted from a count cached in sessionStorage, so that a page without the
    // registry could still show staged changes. No such page has the button any more, and the
    // cache had become purely a way to be wrong first: arriving on Configuration lit the button
    // up on a stale count and the registry switched it off a moment later. A control that offers
    // something and withdraws it is worse than one that takes 200ms to appear.
    document.getElementById('reviewBtn')?.addEventListener('click', () => {
      if (typeof window.NMS._onCommit === 'function') window.NMS._onCommit();
    });

    document.getElementById('refreshBtn')?.addEventListener('click', (e) => {
      const b = e.currentTarget;
      b.classList.add('spinning');
      setTimeout(() => b.classList.remove('spinning'), 600);
      // Re-render the current page: pages register their render via NMS.onRefresh; if none
      // is registered (e.g. an empty skeleton page), fall back to a full reload.
      if (typeof window.NMS._onRefresh === 'function') window.NMS._onRefresh();
      else window.location.reload();
    });
  }

  // Parse an href into { page, tab, proto } so active-state matching can account for
  // query params (settings.html?tab=protocol&proto=snmp etc.), not just the file name.


  // Top-right Config Review control. setPendingChanges(n) is called whenever the staged change
  // count changes (0 => disabled/muted, >0 => active); register what the button opens with
  // onCommit(fn). The button exists only on the pages that stage a candidate (see buildTopbar),
  // so every write here tolerates its absence.
  //
  // The count is NOT cached anywhere. It is a cross-module answer — how many entities differ from
  // the running configuration, across eight tab modules — and the only thing that can compute it
  // is the staging registry, on a page that has loaded all eight. A stored copy of that answer is
  // a copy that can be wrong, and the one place it was read was the one place being wrong showed:
  // the first paint after a navigation.
  //
  // The count, not a flag, because the button carries it as a badge: "Config Review 3" is the one
  // thing an operator can read without opening anything.
  // Opt-in trace of every change to the button's state, for the question this control keeps
  // raising: it went from empty to N and back, and nothing on screen says which of the eight tab
  // modules claimed a candidate. Turn it on with `localStorage.pzDebugReview = 1` and reload.
  function tracePending(count) {
    try {
      if (!localStorage.getItem('pzDebugReview')) return;
      const who = window.NMS.staging ? window.NMS.staging.dirtyKeys() : ['(registry not loaded)'];
      console.info('[pz-review] count=%d at %dms dirty=[%s]', count,
                   Math.round(performance.now()), who.join(', '));
      console.info((new Error().stack || '').split('\n').slice(2, 6).join('\n'));
    } catch (_) { /* tracing must never be the thing that breaks the page */ }
  }

  function applyPending(n) {
    const count = n | 0;
    tracePending(count);
    const btn = document.getElementById('reviewBtn');
    if (btn) btn.disabled = count <= 0;
    const badge = document.getElementById('reviewCount');
    if (badge) {
      badge.textContent = String(count);
      badge.hidden = count <= 0;
    }
  }

  // Sidebar counts, keyed by the badgeId a SIDEBAR_NAV entry declares.
  //
  // The renderer has always emitted the span; until now nothing filled it, so an entry that asked
  // for a badge wore a literal "--". Stored per browser tab next to the drafts and the publish
  // flag, for the same reason they are: the count outlives the page that computed it.
  const NAV_BADGE_KEY = 'pz.navBadge.';

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

  window.NMS.setPendingChanges = function (n) { applyPending(n); };
  // What the Config Review button opens. There is no onRevert beside it: discarding the candidate
  // is reached from inside that dialog (js/commit.js), never from the topbar, so a second hook
  // would be a registration nothing could ever call.
  window.NMS.onCommit = function (fn) { window.NMS._onCommit = fn; };

  // Register how the current page re-renders itself when the topbar Refresh is clicked.
  // Unregistered pages fall back to a full reload.
  window.NMS.onRefresh = function (fn) { window.NMS._onRefresh = fn; };

  window.NMS.topbar = { build: buildTopbar };
})();
