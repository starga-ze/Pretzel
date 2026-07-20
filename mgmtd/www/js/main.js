/* main.js — sidebar injection + shared utilities */
(function () {
  'use strict';

  // ── Sidebar definition ───────────────────────────────────────────────────
  // Single source of truth for all pages.

  const SIDEBAR_NAV = [

    {
      type: 'link', id: 'home', label: 'Home', href: 'home',
      icon: `<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
             <polyline points="9 22 9 12 15 12 15 22"/>`,
    },

    { type: 'section', label: 'Insight' },
    {
      type: 'link', id: 'floor-map', label: 'Floor Map', href: 'rack-management',
      icon: `<polygon points="1 6 8 3 16 6 23 3 23 18 16 21 8 18 1 21 1 6"/>
             <line x1="8" y1="3" x2="8" y2="18"/><line x1="16" y1="6" x2="16" y2="21"/>`,
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

    { type: 'section', label: 'Monitor' },
    {
      type: 'link', id: 'events', label: 'Events', href: 'events',
      icon: `<path d="M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9"/>
             <path d="M13.73 21a2 2 0 0 1-3.46 0"/>`,
    },
    {
      type: 'link', id: 'log-viewer', label: 'Logs', href: 'log-viewer',
      icon: `<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
             <polyline points="14 2 14 8 20 8"/><line x1="8" y1="13" x2="16" y2="13"/>
             <line x1="8" y1="17" x2="16" y2="17"/><line x1="8" y1="9" x2="10" y2="9"/>`,
    },

    { type: 'section', label: 'Administrator' },
    {
      type: 'link', id: 'configuration', label: 'Configuration', href: 'settings',
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
    {
      type: 'link', id: 'audit', label: 'Audit', href: 'audit',
      icon: `<path d="M9 5H7a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V7a2 2 0 0 0-2-2h-2"/>
             <rect x="9" y="3" width="6" height="4" rx="1"/>
             <line x1="9" y1="12" x2="15" y2="12"/><line x1="9" y1="16" x2="13" y2="16"/>`,
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
    'rack-management': { title: 'Floor Map' },
    'log-viewer':      { title: 'Logs' },
    'events':          { title: 'Events' },
    'settings':        { title: 'Configuration', tabs: [
                           { id: 'inventory', label: 'Inventory' },
                           { id: 'system',    label: 'System'    } ] },
    'audit':           { title: 'Audit', tabs: [
                           { id: 'config', label: 'Config History' },
                           { id: 'access', label: 'Access History' } ] },
    'laboratory':      { title: 'Laboratory' },
  };

  function buildTopbar() {
    const el = document.getElementById('pageTopbar');
    if (!el) return;

    const page = location.pathname.split('/').pop() || 'home';
    const cfg = PAGES[page];
    if (!cfg) { el.remove(); return; }

    document.title = 'Pretzel — ' + cfg.title;

    const params = new URLSearchParams(location.search);
    const activeTab = params.get('tab') || (cfg.tabs ? cfg.tabs[0].id : '');

    let html = `
      <div class="topbar-main">
        <h1 class="topbar-title">${cfg.title}</h1>
        <div class="topbar-actions">
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

    if (cfg.tabs) {
      el.classList.add('has-tabs');
      html += `<nav class="page-tabs">` + cfg.tabs.map(t =>
        `<a class="page-tab ${t.id === activeTab ? 'active' : ''}" href="${page}?tab=${t.id}">${t.label}</a>`
      ).join('') + `</nav>`;
    }

    el.innerHTML = html;

    document.getElementById('commitBtn')?.addEventListener('click', () => {
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
    const activeTab   = params.get('tab')   || '';
    const activeProto = params.get('proto') || '';

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
        const subs = item.subitems.map(s => ({
          id: s.id, label: s.label, href: s.href,
          soon: !!s.soon, active: hrefActive(s.href, false),
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
          return `<a class="${cls}" href="${href}">` +
                 `<span>${s.label}</span>` +
                 (s.soon ? '<span class="nav-badge-soon">Soon</span>' : '') +
                 `</a>`;
        }).join('');

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

  // Top-right Publish button control. A page calls setPendingChanges(n) whenever its staged
  // change count changes (0 => disabled/muted, >0 => active); register the deploy action
  // with onCommit(fn).
  window.NMS.setPendingChanges = function (n) {
    const btn = document.getElementById('commitBtn');
    if (btn) btn.disabled = (n | 0) <= 0;   // label stays "Publish"; only enabled state changes
  };
  window.NMS.onCommit = function (fn) { window.NMS._onCommit = fn; };

  // Register how the current page re-renders itself when the topbar Refresh is clicked.
  // Unregistered pages fall back to a full reload.
  window.NMS.onRefresh = function (fn) { window.NMS._onRefresh = fn; };

  window.NMS.utils = {
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
  };
}());
