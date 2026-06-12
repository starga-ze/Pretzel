/* main.js — sidebar injection + shared utilities */
(function () {
  'use strict';

  // ── Sidebar definition ───────────────────────────────────────────────────
  // Single source of truth for all pages.

  const SIDEBAR_NAV = [

    {
      type: 'link', id: 'dashboard', label: 'Dashboard', href: 'dashboard.html',
      icon: `<path d="M3 3v18h18"/>
             <polyline points="7 14 11 10 14 13 19 7"/>`,
    },
    {
      type: 'group', id: 'resource', label: 'Resource', badgeId: 'aliveCount',
      icon: `<rect x="2" y="2" width="20" height="8" rx="2"/>
             <rect x="2" y="14" width="20" height="8" rx="2"/>
             <line x1="6" y1="6" x2="6.01" y2="6"/>
             <line x1="6" y1="18" x2="6.01" y2="18"/>`,
      subitems: [
        { id: 'devices',         label: 'Devices',         href: 'devices.html' },
        { id: 'rack-management', label: 'Rack Management', href: '#', soon: true },
      ],
    },
    {
      type: 'group', id: 'topology', label: 'Topology',
      icon: `<circle cx="12" cy="5" r="2"/>
             <circle cx="5" cy="19" r="2"/>
             <circle cx="19" cy="19" r="2"/>
             <line x1="12" y1="7" x2="5" y2="17"/>
             <line x1="12" y1="7" x2="19" y2="17"/>
             <line x1="5" y1="19" x2="19" y2="19"/>`,
      subitems: [
        { id: 'cable-map',   label: 'Cable Map',   href: '#', soon: true },
        { id: 'port-map',    label: 'Port Map',    href: '#', soon: true },
        { id: 'l3-topology', label: 'L3 Topology', href: '#', soon: true },
      ],
    },
    {
      type: 'group', id: 'control', label: 'Control',
      icon: `<line x1="4" y1="21" x2="4" y2="14"/><line x1="4" y1="10" x2="4" y2="3"/>
             <line x1="12" y1="21" x2="12" y2="12"/><line x1="12" y1="8" x2="12" y2="3"/>
             <line x1="20" y1="21" x2="20" y2="16"/><line x1="20" y1="12" x2="20" y2="3"/>
             <line x1="1" y1="14" x2="7" y2="14"/><line x1="9" y1="8" x2="15" y2="8"/>
             <line x1="17" y1="16" x2="23" y2="16"/>`,
      subitems: [
        { id: 'schedule',             label: 'Schedule',             href: '#', soon: true },
        { id: 'power-remote-control', label: 'Power Remote Control', href: '#', soon: true },
        { id: 'remote-access',        label: 'Remote Access',        href: '#', soon: true },
      ],
    },
    {
      type: 'link', id: 'log-viewer', label: 'Log Viewer', href: 'log-viewer.html',
      icon: `<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
             <polyline points="14 2 14 8 20 8"/>
             <line x1="8" y1="13" x2="16" y2="13"/>
             <line x1="8" y1="17" x2="16" y2="17"/>
             <line x1="8" y1="9" x2="10" y2="9"/>`,
    },
    {
      type: 'group', id: 'configuration', label: 'Configuration',
      icon: `<path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77
                      a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91
                      a6 6 0 0 1 7.94-7.94l-3.76 3.76z"/>`,
      subitems: [
        { id: 'general',  label: 'General',  href: 'settings.html?tab=general' },
        { id: 'protocol', label: 'Protocol', href: 'settings.html?tab=protocol' },
        { id: 'user',     label: 'User',     href: 'settings.html?tab=users', soon: true },
      ],
    },
    {
      type: 'group', id: 'system', label: 'System',
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
      subitems: [
        { id: 'overview', label: 'Overview', href: '#', soon: true },
      ],
    },
  ];

  function buildSvg(inner) {
    return `<svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">${inner}</svg>`;
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

    const activePage  = location.pathname.split('/').pop() || 'dashboard.html';
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
        <div class="nav-item" data-tooltip="v0.1.0-dev"
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

  // ── Alive count badge ─────────────────────────────────────────────────────

  async function pollAliveCount() {
    try {
      const r = await fetch('/api/status', { credentials: 'same-origin' });
      if (!r.ok) return;
      const d = await r.json();
      const el = document.getElementById('aliveCount');
      if (el && d.alive_devices != null) el.textContent = d.alive_devices;
    } catch { /* ignore */ }
  }

  // ── Init ──────────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    buildSidebar();
    initSidebar();
    initFlyouts();
    initTooltips();
    pollAliveCount();
  });

  /* ── Shared utils ── */
  window.NMS = window.NMS || {};
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
      if (r.status === 401) { window.location.href = '/index.html'; return null; }
      if (!r.ok) throw new Error(r.status);
      return r.json();
    },
  };
}());
