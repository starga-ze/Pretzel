/* main.js — sidebar injection + shared utilities */
(function () {
  'use strict';

  // ── Configuration groups ─────────────────────────────────────────────────
  // One definition drives both navigation levels: the sidebar flyout lists the groups, and the
  // topbar shows the tabs of whichever group is open. Grouped by what the operator is working
  // on rather than by data type —
  //   Site Management    where things are, and what is there (a Site is one customer)
  //   API Profile        the reusable definitions a connector references
  //   API Connector      binding a device + credential + endpoints on a schedule
  //   System Management  this appliance, not the managed estate
  // A group with a single tab renders no tab row; its name in the sidebar is the whole story.
  const SETTINGS_GROUPS = [
    { id: 'site-management', label: 'Site Management', tabs: [
        { id: 'sites',        label: 'Sites'        },
        { id: 'devices',      label: 'Devices'      } ] },
    { id: 'api-profile', label: 'API Profile', tabs: [
        { id: 'api-key',      label: 'API Key'      },
        { id: 'api-endpoint', label: 'API Endpoint' } ] },
    { id: 'api-connector', label: 'API Connector', tabs: [
        { id: 'api-connector', label: 'API Connector' } ] },
    { id: 'system-management', label: 'System Management', tabs: [
        { id: 'user',         label: 'User'         },
        { id: 'operation',    label: 'Operation'    } ] },
  ];

  const SETTINGS_TABS = SETTINGS_GROUPS.reduce((acc, g) => acc.concat(g.tabs), []);
  const groupOfTab = (tabId) =>
    SETTINGS_GROUPS.find(g => g.tabs.some(t => t.id === tabId)) || SETTINGS_GROUPS[0];

  // ── Sidebar definition ───────────────────────────────────────────────────
  // Single source of truth for all pages.

  const SIDEBAR_NAV = [

    {
      type: 'link', id: 'home', label: 'Home', href: 'home',
      icon: `<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
             <polyline points="9 22 9 12 15 12 15 22"/>`,
    },

    { type: 'section', label: 'Control' },
    {
      type: 'link', id: 'remote-access', label: 'Remote Access', href: '#', soon: true,
      icon: `<polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/>`,
    },
    {
      type: 'link', id: 'power', label: 'Power Control', href: '#', soon: true,
      icon: `<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/>`,
    },

    { type: 'section', label: 'Administrator' },
    {
      // A flyout group, not a link: the groups live here so the topbar carries a single row of
      // tabs. Each subitem opens its group's first tab.
      type: 'group', id: 'configuration', label: 'Configuration',
      subitems: SETTINGS_GROUPS.map(g => ({
        id: g.id,
        label: g.label,
        href: `settings?tab=${g.tabs[0].id}`,
        tabs: g.tabs.map(t => t.id),
      })),
      icon: `<circle cx="12" cy="12" r="3"/>
             <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06
                      a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09
                      A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83
                      l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09
                      A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83
                      l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09
                      a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83
                      l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09
                      a1.65 1.65 0 0 0-1.51 1z"/>`,
    },
    { type: 'section', label: 'Monitor' },
    {
      type: 'link', id: 'system-log', label: 'System Log', href: 'log-viewer',
      icon: `<path d="M4 5h16"/><path d="M4 12h10"/><path d="M4 19h7"/>
             <path d="M15 16l2.5 3 3.5-6"/>`,
    },

    { type: 'section', label: 'Laboratory' },
    {
      type: 'link', id: 'laboratory', label: 'Laboratory', href: 'laboratory',
      icon: `<path d="M9 3h6"/>
             <path d="M10 3v6.5L4.6 18.2A2 2 0 0 0 6.3 21h11.4a2 2 0 0 0 1.7-2.8L14 9.5V3"/>
             <line x1="7" y1="15" x2="17" y2="15"/>`,
    },
  ];

  function buildSvg(inner) {
    return `<svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">${inner}</svg>`;
  }

  // Per-page top layout: title + optional detail tabs. Single source of truth so every
  // page gets the same fixed header band (injected into #pageTopbar by buildTopbar).
  const PAGES = {
    'home':            { title: 'Home' },
    // The group is chosen in the sidebar flyout (SETTINGS_GROUPS); the topbar shows that
    // group's name and one row of its tabs.
    'settings':        { title: 'Configuration', groups: SETTINGS_GROUPS },
    'log-viewer':      { title: 'System Log' },
    'laboratory':      { title: 'Laboratory' },
  };

  // The flyout is rebuilt from the toggle's data-subitems each time it opens, so marking the
  // active group there is enough to keep it correct after an in-place tab switch.
  function syncFlyoutActive(tabId) {
    const toggle = document.querySelector('.nav-group[data-group-id="configuration"] .nav-group-toggle');
    if (!toggle) return;
    let subs;
    try { subs = JSON.parse(toggle.dataset.subitems || '[]'); } catch (_) { return; }
    subs.forEach(s => { s.active = !!(s.tabs && s.tabs.indexOf(tabId) !== -1); });
    toggle.dataset.subitems = JSON.stringify(subs);
  }

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

    let html = `
      <div class="topbar-main">
        <h1 class="topbar-title">${heading}</h1>
        <div class="topbar-actions">
          <button class="topbar-view" id="viewBtn" type="button">View</button>
          <button class="topbar-revert" id="revertBtn" type="button" disabled>Revert</button>
          <button class="topbar-commit" id="commitBtn" type="button" disabled>
            <span id="commitLabel">Publish</span>
          </button>
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
    // staged state, so the topbar — and the Publish/Revert pending state — survives the switch
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

        // Keep the sidebar's flyout highlight in step. Only the stored subitem state is
        // rewritten — rebuilding the sidebar would mean re-running initFlyouts, whose
        // document/window listeners would then accumulate on every switch.
        syncFlyoutActive(tabId);

        document.dispatchEvent(new CustomEvent('nms:tab-change', { detail: { tab: tabId, group: group.id } }));
      }

      el.querySelectorAll('.page-tab').forEach(a => a.addEventListener('click', onTabClick));
      window.addEventListener('popstate', () => switchTab(currentTab(), false));

      // The sidebar flyout navigates between groups. While already on this page that should be
      // the same in-place switch as a tab click — a full load would rebuild the topbar and make
      // the Publish/Revert state flicker back through disabled.
      window.NMS = window.NMS || {};
      window.NMS.gotoSettingsTab = (tabId) => {
        if (!allTabs.some(t => t.id === tabId) || tabId === currentTab()) return false;
        switchTab(tabId, true);
        return true;
      };
    }

    document.getElementById('commitBtn')?.addEventListener('click', () => {
      if (typeof window.NMS._onCommit === 'function') window.NMS._onCommit();
    });

    document.getElementById('revertBtn')?.addEventListener('click', () => {
      if (typeof window.NMS._onRevert === 'function') window.NMS._onRevert();
    });

    // View is always available — it reads the committed configuration, so it does not depend on
    // anything being staged (see NMS.viewRunningConfig in js/commit.js).
    document.getElementById('viewBtn')?.addEventListener('click', () => {
      if (typeof window.NMS.viewRunningConfig === 'function') window.NMS.viewRunningConfig();
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
  function parseHref(href) {
    const [page, query] = String(href || '').split('?');
    const p = new URLSearchParams(query || '');
    return { page, tab: p.get('tab') || '', proto: p.get('proto') || '' };
  }

  function buildSidebar() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;

    const activePage  = location.pathname.split('/').pop() || 'home';
    const params      = new URLSearchParams(location.search);
    const activeProto = params.get('proto') || '';
    // Settings without ?tab= lands on the first tab, so resolve it here too or the sidebar
    // would show no group highlighted on a bare /settings.
    const requestedTab = params.get('tab') || '';
    const activeTab = (activePage === 'settings' && !SETTINGS_TABS.some(t => t.id === requestedTab))
      ? SETTINGS_TABS[0].id
      : requestedTab;

    // A link/subitem is active when its page matches and any tab/proto it pins matches.
    const hrefActive = (href, disabled) => {
      if (disabled) return false;
      const h = parseHref(href);
      if (!h.page || h.page === '#' || h.page !== activePage) return false;
      if (h.tab   && h.tab   !== activeTab)   return false;
      if (h.proto && h.proto !== activeProto) return false;
      return true;
    };

    let html = `
      <div class="sidebar-brand">
        <div class="brand-icon">P</div>
        <span class="brand-name">Pretzel</span>
        <button class="sidebar-toggle" id="sidebarToggle" aria-label="Collapse sidebar">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
               stroke="currentColor" stroke-width="2" stroke-linecap="round">
            <rect x="3" y="3" width="18" height="18" rx="2"/>
            <line x1="9" y1="3" x2="9" y2="21"/>
            <polyline points="15 9 12 12 15 15"/>
          </svg>
        </button>
      </div>
      <nav class="sidebar-nav">`;

    for (const item of SIDEBAR_NAV) {
      if (item.type === 'section') {
        html += `<div class="nav-section-label">${item.label}</div>`;
        continue;
      }

      if (item.type === 'link') {
        const active = hrefActive(item.href, item.disabled);
        const cls = ['nav-item', active ? 'active' : '', item.disabled ? 'nav-disabled' : ''].filter(Boolean).join(' ');
        const href = item.disabled ? '#' : item.href;
        const tooltip = item.disabled ? `${item.label} (Coming soon)` : item.label;
        html += `<a class="${cls}" href="${href}" data-page="${item.href || ''}" data-tooltip="${tooltip}">
          ${buildSvg(item.icon)}
          <span class="nav-label">${item.label}</span>
          ${item.soon ? '<span class="nav-badge-soon">Coming soon</span>' : ''}
          ${item.badgeId ? `<span class="nav-badge" id="${item.badgeId}">--</span>` : ''}
        </a>`;
        continue;
      }

      if (item.type === 'group') {
        // Flyout group: the toggle never navigates; hovering/clicking it reveals a
        // flyout (built by initFlyouts from data-subitems) to the right — works the
        // same expanded or collapsed. The group is "active" if a subitem is active.
        //
        // A subitem links to its group's first tab, so matching on the href alone would drop
        // the highlight the moment the operator moved to a second tab. When the subitem lists
        // the tabs it owns, membership decides instead.
        const subs = item.subitems.map(s => ({
          id: s.id, label: s.label, href: s.href, tabs: s.tabs || null,
          soon: !!s.soon,
          active: s.tabs
            ? (parseHref(s.href).page === activePage && s.tabs.indexOf(activeTab) !== -1)
            : hrefActive(s.href, false),
        }));
        const groupActive = subs.some(s => s.active);
        const cls = ['nav-item', 'nav-group-toggle', groupActive ? 'active' : ''].filter(Boolean).join(' ');
        const dataSubs = JSON.stringify(subs).replace(/&/g, '&amp;').replace(/"/g, '&quot;');
        html += `<div class="nav-group" data-group-id="${item.id}">
          <button type="button" class="${cls}" data-tooltip="${item.label}"
                  data-label="${item.label}" data-subitems="${dataSubs}" aria-haspopup="true" aria-expanded="false">
            ${buildSvg(item.icon)}
            <span class="nav-label">${item.label}</span>
            ${item.badgeId ? `<span class="nav-badge" id="${item.badgeId}">--</span>` : ''}
            <svg class="nav-chevron" width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2" stroke-linecap="round">
              <polyline points="9 6 15 12 9 18"/>
            </svg>
          </button>
        </div>`;
      }
    }

    html += `</nav>
      <div class="sidebar-footer">
        <div class="nav-item nav-user" id="navUser" data-tooltip="Signed in">
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/>
            <circle cx="12" cy="7" r="4"/>
          </svg>
          <span class="nav-label nav-user-name" id="navUserName">--</span>
        </div>
        <button type="button" class="nav-item nav-logout" id="navLogout" data-tooltip="Log out">
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/>
            <polyline points="16 17 21 12 16 7"/>
            <line x1="21" y1="12" x2="9" y2="12"/>
          </svg>
          <span class="nav-label">Log out</span>
        </button>
        <div class="nav-item nav-version" data-tooltip="v0.1.0-dev"
             style="cursor:default;opacity:.35;pointer-events:none;">
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <circle cx="12" cy="12" r="10"/>
            <line x1="12" y1="8" x2="12" y2="12"/>
            <line x1="12" y1="16" x2="12.01" y2="16"/>
          </svg>
          <span class="nav-label">v0.1.0-dev</span>
        </div>
      </div>`;

    sidebar.innerHTML = html;
  }

  // ── Sidebar collapse/expand ───────────────────────────────────────────────

  const COLLAPSED_KEY = 'sidebarCollapsed';

  function initSidebar() {
    const sidebar       = document.getElementById('sidebar');
    const overlay       = document.getElementById('sidebarOverlay');
    const railToggle    = document.getElementById('railToggle');
    const railToggleBtn = document.getElementById('railToggleBtn');
    if (!sidebar) return;

    const isMobile = () => window.innerWidth <= 680;

    function isCollapsed() { return sidebar.classList.contains('collapsed'); }

    function applyCollapsed(collapsed) {
      sidebar.classList.toggle('collapsed', collapsed);
      if (railToggle)
        railToggle.style.display = (!isMobile() && collapsed) ? 'flex' : 'none';
    }

    if (isMobile()) {
      sidebar.classList.remove('collapsed');
      if (railToggle) railToggle.style.display = 'flex';
    } else {
      applyCollapsed(localStorage.getItem(COLLAPSED_KEY) === 'true');
    }

    // The pre-paint width is now owned by the .collapsed class (applied above); drop
    // the bootstrap html class so it can't override width once the user toggles.
    document.documentElement.classList.remove('sb-init-collapsed');

    // Toggle button is injected by buildSidebar — need to bind after inject
    const toggleBtn = document.getElementById('sidebarToggle');
    toggleBtn?.addEventListener('click', () => {
      const next = !isCollapsed();
      applyCollapsed(next);
      localStorage.setItem(COLLAPSED_KEY, String(next));
    });

    function openMobile()  { sidebar.classList.add('mobile-open'); overlay?.classList.add('visible'); if (railToggle) railToggle.style.display = 'none'; }
    function closeMobile() { sidebar.classList.remove('mobile-open'); overlay?.classList.remove('visible'); if (railToggle) railToggle.style.display = 'flex'; }

    railToggleBtn?.addEventListener('click', () => {
      isMobile() ? openMobile() : applyCollapsed(false) || localStorage.setItem(COLLAPSED_KEY, 'false');
    });
    overlay?.addEventListener('click', closeMobile);

    window.addEventListener('resize', () => {
      if (!isMobile()) {
        sidebar.classList.remove('mobile-open');
        overlay?.classList.remove('visible');
        applyCollapsed(localStorage.getItem(COLLAPSED_KEY) === 'true');
      } else {
        sidebar.classList.remove('collapsed');
        applyCollapsed(false);
        if (railToggle) railToggle.style.display = 'flex';
      }
    });
  }

  // ── Nav group flyouts ─────────────────────────────────────────────────────
  // Groups don't expand inline; hovering/clicking the toggle reveals a flyout to the
  // right. The panel is a single shared element appended to <body> so the sidebar's
  // overflow never clips it — works identically whether the rail is expanded or
  // collapsed, satisfying "still selectable when collapsed".

  function initFlyouts() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;

    let flyout = document.getElementById('navFlyout');
    if (!flyout) {
      flyout = document.createElement('div');
      flyout.id = 'navFlyout';
      flyout.className = 'nav-flyout';
      document.body.appendChild(flyout);
    }

    let hideTimer = null;
    const cancelHide   = () => clearTimeout(hideTimer);
    const scheduleHide = () => { clearTimeout(hideTimer); hideTimer = setTimeout(hide, 160); };

    function hide() {
      flyout.classList.remove('visible');
      sidebar.querySelectorAll('.nav-group-toggle[aria-expanded="true"]')
        .forEach(t => t.setAttribute('aria-expanded', 'false'));
    }

    function show(group) {
      const toggle = group.querySelector('.nav-group-toggle');
      if (!toggle) return;

      let subs = [];
      try { subs = JSON.parse(toggle.dataset.subitems || '[]'); } catch (_) { /* ignore */ }

      flyout.innerHTML =
        `<div class="nav-flyout-title">${toggle.dataset.label || ''}</div>` +
        subs.map(s => {
          const cls  = ['nav-flyout-item', s.active ? 'active' : '', s.soon ? 'is-soon' : ''].filter(Boolean).join(' ');
          const href = s.soon ? '#' : s.href;
          const tab  = (s.tabs && s.tabs.length) ? ` data-goto-tab="${s.tabs[0]}"` : '';
          return `<a class="${cls}" href="${href}"${tab}>` +
                 `<span>${s.label}</span>` +
                 (s.soon ? '<span class="nav-badge-soon">Soon</span>' : '') +
                 `</a>`;
        }).join('');

      // Already on the target page: switch in place rather than reloading, so the topbar and
      // the staged Publish/Revert state survive (see NMS.gotoSettingsTab).
      flyout.querySelectorAll('[data-goto-tab]').forEach(a => {
        a.addEventListener('click', (e) => {
          if (typeof window.NMS.gotoSettingsTab !== 'function') return;
          e.preventDefault();
          window.NMS.gotoSettingsTab(a.dataset.gotoTab);
          hide();
        });
      });

      // Anchor to the right edge of the toggle, clamped to the viewport.
      const r = toggle.getBoundingClientRect();
      flyout.style.visibility = 'hidden';
      flyout.classList.add('visible');
      const fh = flyout.offsetHeight;
      let top = r.top;
      if (top + fh > window.innerHeight - 8) top = Math.max(8, window.innerHeight - 8 - fh);
      flyout.style.top  = `${top}px`;
      flyout.style.left = `${r.right + 6}px`;
      flyout.style.visibility = '';

      sidebar.querySelectorAll('.nav-group-toggle[aria-expanded="true"]')
        .forEach(t => t.setAttribute('aria-expanded', 'false'));
      toggle.setAttribute('aria-expanded', 'true');
    }

    sidebar.querySelectorAll('.nav-group').forEach(group => {
      const toggle = group.querySelector('.nav-group-toggle');
      group.addEventListener('mouseenter', () => { cancelHide(); show(group); });
      group.addEventListener('mouseleave', scheduleHide);
      toggle?.addEventListener('click', (e) => { e.preventDefault(); cancelHide(); show(group); });
    });

    flyout.addEventListener('mouseenter', cancelHide);
    flyout.addEventListener('mouseleave', scheduleHide);

    // Dismiss when clicking away, scrolling, or resizing (anchor would go stale).
    document.addEventListener('click', (e) => {
      if (!flyout.contains(e.target) && !e.target.closest('.nav-group')) hide();
    });
    window.addEventListener('scroll', hide, true);
    window.addEventListener('resize', hide);
  }

  // ── Global hover tooltip ──────────────────────────────────────────────────
  // Any element with [data-tip] shows a small fixed-position tooltip on hover/focus.
  // Event-delegated, so it covers content rendered dynamically after load.

  function initTooltips() {
    let tip = document.getElementById('appTooltip');
    if (!tip) {
      tip = document.createElement('div');
      tip.id = 'appTooltip';
      tip.className = 'app-tooltip';
      document.body.appendChild(tip);
    }

    function show(el) {
      const text = el.getAttribute('data-tip');
      if (!text) return;
      tip.textContent = text;
      tip.classList.add('visible');
      const r  = el.getBoundingClientRect();
      const tr = tip.getBoundingClientRect();
      let left = r.left + r.width / 2 - tr.width / 2;
      left = Math.max(8, Math.min(left, window.innerWidth - tr.width - 8));
      let top = r.top - tr.height - 8;
      if (top < 8) top = r.bottom + 8;   // flip below if no room above
      tip.style.left = `${left}px`;
      tip.style.top  = `${top}px`;
    }
    function hide() { tip.classList.remove('visible'); }

    document.addEventListener('mouseover', e => {
      const t = e.target.closest('[data-tip]');
      if (t) show(t);
    });
    document.addEventListener('mouseout', e => {
      const t = e.target.closest('[data-tip]');
      if (t && !t.contains(e.relatedTarget)) hide();
    });
    document.addEventListener('focusin',  e => { const t = e.target.closest('[data-tip]'); if (t) show(t); });
    document.addEventListener('focusout', hide);
    window.addEventListener('scroll', hide, true);
  }

  // ── User footer (identity + logout) ───────────────────────────────────────
  // Fills the signed-in user's name (GET /api/whoami) and wires the logout button
  // (POST /api/logout, then back to the login page). Failures degrade quietly — the
  // footer just shows a dash rather than blocking the page.

  function initUserFooter() {
    const nameEl = document.getElementById('navUserName');
    const userEl = document.getElementById('navUser');

    fetch('/api/whoami', { credentials: 'same-origin', headers: { Accept: 'application/json' } })
      .then(r => (r.ok ? r.json() : null))
      .then(info => {
        const name = info && info.username ? info.username : '—';
        if (nameEl) nameEl.textContent = name;
        if (userEl) userEl.setAttribute('data-tooltip', name);
      })
      .catch(() => { if (nameEl) nameEl.textContent = '—'; });

    document.getElementById('navLogout')?.addEventListener('click', async () => {
      try {
        await fetch('/api/logout', { method: 'POST', credentials: 'same-origin' });
      } catch (_) { /* logout is best-effort; redirect regardless */ }
      window.location.href = '/';
    });
  }

  // ── Init ──────────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    buildSidebar();
    buildTopbar();
    initSidebar();
    initFlyouts();
    initTooltips();
    initUserFooter();
  });

  /* ── Shared utils ── */
  window.NMS = window.NMS || {};

  // Top-right Publish/Revert button control. setPendingChanges(n) is called whenever the staged
  // change count changes (0 => both disabled/muted, >0 => both active); register the deploy and
  // discard actions with onCommit(fn) / onRevert(fn).
  window.NMS.setPendingChanges = function (n) {
    const dirty = (n | 0) > 0;
    const commit = document.getElementById('commitBtn');
    const revert = document.getElementById('revertBtn');
    if (commit) commit.disabled = !dirty;   // label stays "Publish"; only enabled state changes
    if (revert) revert.disabled = !dirty;
  };
  window.NMS.onCommit = function (fn) { window.NMS._onCommit = fn; };
  window.NMS.onRevert = function (fn) { window.NMS._onRevert = fn; };

  // Register how the current page re-renders itself when the topbar Refresh is clicked.
  // Unregistered pages fall back to a full reload.
  window.NMS.onRefresh = function (fn) { window.NMS._onRefresh = fn; };

  // Only one custom-select dropdown is open at a time; opening a second closes the first.
  let csActiveClose = null;

  window.NMS.utils = {
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
    async fetchJSON(url, opts) {
      const r = await fetch(url, Object.assign({ credentials: 'same-origin' }, opts));
      if (r.status === 401) { window.location.href = '/'; return null; }
      if (!r.ok) throw new Error(r.status);
      return r.json();
    },

    // Enhance every <select> in a container with the custom dropdown below.
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
        window.removeEventListener('scroll', close, true);
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
        window.addEventListener('scroll', close, true);
        csActiveClose = close;
      };

      trigger.addEventListener('click', open);
      select.addEventListener('change', syncLabel);
      syncLabel();
    },
  };
}());
