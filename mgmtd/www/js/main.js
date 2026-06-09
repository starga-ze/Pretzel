/* main.js — sidebar injection + shared utilities */
(function () {
  'use strict';

  // ── Sidebar definition ───────────────────────────────────────────────────
  // Single source of truth for all pages.

  const SIDEBAR_NAV = [

    // ── Overview ──────────────────────────────────────────
    { type: 'section', label: 'Overview' },
    {
      type: 'link', id: 'dashboard', label: 'Dashboard', href: 'dashboard.html',
      icon: `<rect x="3" y="3" width="7" height="7" rx="1"/>
             <rect x="14" y="3" width="7" height="7" rx="1"/>
             <rect x="14" y="14" width="7" height="7" rx="1"/>
             <rect x="3" y="14" width="7" height="7" rx="1"/>`,
    },
    {
      type: 'link', id: 'insight', label: 'Insight', href: '#', disabled: true, soon: true,
      icon: `<circle cx="12" cy="12" r="10"/>
             <polyline points="12 6 12 12 16 14"/>`,
    },
    {
      type: 'link', id: 'devices', label: 'Devices', href: 'devices.html',
      badgeId: 'aliveCount',
      icon: `<rect x="2" y="2" width="20" height="8" rx="2"/>
             <rect x="2" y="14" width="20" height="8" rx="2"/>
             <line x1="6" y1="6" x2="6.01" y2="6"/>
             <line x1="6" y1="18" x2="6.01" y2="18"/>`,
    },

    // ── Visibility ────────────────────────────────────────
    { type: 'section', label: 'Visibility' },
    {
      type: 'link', id: 'rack-view', label: 'Rack View', href: '#', disabled: true, soon: true,
      icon: `<rect x="2" y="2" width="20" height="8" rx="1"/>
             <rect x="2" y="12" width="20" height="4" rx="1"/>
             <rect x="2" y="18" width="20" height="4" rx="1"/>
             <circle cx="18" cy="6" r="1.2" fill="currentColor"/>`,
    },
    {
      type: 'link', id: 'cable-map', label: 'Cable Map', href: '#', disabled: true, soon: true,
      icon: `<path d="M4 12h16M4 12a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2"/>
             <path d="M4 12a2 2 0 0 0-2 2v4a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-4a2 2 0 0 0-2-2"/>`,
    },
    {
      type: 'link', id: 'port-map', label: 'Port Map', href: '#', disabled: true, soon: true,
      icon: `<rect x="3" y="3" width="5" height="5" rx="1"/>
             <rect x="10" y="3" width="5" height="5" rx="1"/>
             <rect x="17" y="3" width="5" height="5" rx="1"/>
             <rect x="3" y="10" width="5" height="5" rx="1"/>
             <rect x="10" y="10" width="5" height="5" rx="1"/>
             <rect x="17" y="10" width="5" height="5" rx="1"/>`,
    },
    {
      type: 'link', id: 'l2-topology', label: 'L2 Topology', href: '#', disabled: true, soon: true,
      icon: `<circle cx="12" cy="5" r="2"/>
             <circle cx="5" cy="19" r="2"/>
             <circle cx="19" cy="19" r="2"/>
             <line x1="12" y1="7" x2="5" y2="17"/>
             <line x1="12" y1="7" x2="19" y2="17"/>
             <line x1="5" y1="19" x2="19" y2="19"/>`,
    },
    {
      type: 'link', id: 'l3-topology', label: 'L3 Topology', href: '#', disabled: true, soon: true,
      icon: `<circle cx="12" cy="5" r="2"/>
             <circle cx="3" cy="19" r="2"/>
             <circle cx="12" cy="19" r="2"/>
             <circle cx="21" cy="19" r="2"/>
             <line x1="12" y1="7" x2="3" y2="17"/>
             <line x1="12" y1="7" x2="12" y2="17"/>
             <line x1="12" y1="7" x2="21" y2="17"/>`,
    },

    // ── Operations ────────────────────────────────────────
    { type: 'section', label: 'Operations' },
    {
      type: 'link', id: 'power-control', label: 'Power Control', href: '#', disabled: true, soon: true,
      icon: `<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/>
             <line x1="12" y1="2" x2="12" y2="12"/>`,
    },
    {
      type: 'link', id: 'schedule', label: 'Schedule', href: '#', disabled: true, soon: true,
      icon: `<rect x="3" y="4" width="18" height="18" rx="2"/>
             <line x1="16" y1="2" x2="16" y2="6"/>
             <line x1="8" y1="2" x2="8" y2="6"/>
             <line x1="3" y1="10" x2="21" y2="10"/>`,
    },
    {
      type: 'link', id: 'remote-access', label: 'Remote Access', href: '#', disabled: true, soon: true,
      icon: `<rect x="2" y="3" width="20" height="14" rx="2"/>
             <line x1="8" y1="21" x2="16" y2="21"/>
             <line x1="12" y1="17" x2="12" y2="21"/>
             <polyline points="8 10 12 14 16 10"/>`,
    },

    // ── Monitor ───────────────────────────────────────────
    { type: 'section', label: 'Monitor' },
    {
      type: 'link', id: 'log-viewer', label: 'Log Viewer', href: 'log-viewer.html',
      icon: `<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
             <polyline points="14 2 14 8 20 8"/>
             <line x1="8" y1="13" x2="16" y2="13"/>
             <line x1="8" y1="17" x2="16" y2="17"/>
             <line x1="8" y1="9" x2="10" y2="9"/>`,
    },
    {
      type: 'link', id: 'event-viewer', label: 'Event Viewer', href: '#', disabled: true, soon: true,
      icon: `<path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/>`,
    },

    // ── Settings ──────────────────────────────────────────
    { type: 'section', label: 'System' },
    {
      type: 'group', id: 'settingsGroup', label: 'Settings',
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
        { id: 'general', label: 'General', href: 'settings.html?tab=general' },
        { id: 'icmp',    label: 'ICMP',    href: 'settings.html?tab=icmp' },
        { id: 'snmp',    label: 'SNMP',    href: 'settings.html?tab=snmp',  soon: true },
        { id: 'lldp',    label: 'LLDP',    href: 'settings.html?tab=lldp',  soon: true },
        { id: 'users',   label: 'Users',   href: 'settings.html?tab=users', soon: true },
      ],
    },
  ];

  function buildSvg(inner) {
    return `<svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">${inner}</svg>`;
  }

  function buildSidebar() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;

    const page = location.pathname.split('/').pop() || 'dashboard.html';
    const tab  = new URLSearchParams(location.search).get('tab') || '';

    // Determine active IDs
    const activePage = page;
    const activeTab  = tab;

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
        const active = item.href && item.href.split('?')[0] === activePage && !item.disabled;
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
        // Group is "active" if current page is settings.html
        const groupActive = activePage === 'settings.html';
        const groupId = `navGroup_${item.id}`;
        html += `<div class="nav-group${groupActive ? ' expanded' : ''}" id="${groupId}">
          <button type="button" class="nav-item nav-group-toggle" data-tooltip="${item.label}" aria-expanded="${groupActive}">
            ${buildSvg(item.icon)}
            <span class="nav-label">${item.label}</span>
            <svg class="nav-chevron" width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2" stroke-linecap="round">
              <polyline points="6 9 12 15 18 9"/>
            </svg>
          </button>
          <div class="nav-subnav">`;

        for (const sub of item.subitems) {
          const subActive = groupActive && activeTab === sub.id;
          const subCls = ['nav-subitem', subActive ? 'active' : ''].filter(Boolean).join(' ');
          html += `<a class="${subCls}" data-tab="${sub.id}" href="${sub.href}">
            <span class="nav-label">${sub.label}</span>
            ${sub.soon ? '<span class="nav-badge-soon">Coming soon</span>' : ''}
          </a>`;
        }

        html += `</div></div>`;
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

  // ── Nav group toggle ──────────────────────────────────────────────────────

  function initNavGroups() {
    document.querySelectorAll('.nav-group').forEach(group => {
      const toggle = group.querySelector('.nav-group-toggle');
      if (!toggle) return;
      const key = `navGroupExpanded:${group.id}`;

      function setExpanded(v) {
        group.classList.toggle('expanded', v);
        toggle.setAttribute('aria-expanded', String(v));
      }

      // Restore saved state (current state already set by buildSidebar)
      const saved = localStorage.getItem(key);
      if (saved !== null) setExpanded(saved === 'true');

      toggle.addEventListener('click', () => {
        const next = !group.classList.contains('expanded');
        setExpanded(next);
        localStorage.setItem(key, String(next));
      });
    });
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
    initNavGroups();
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
