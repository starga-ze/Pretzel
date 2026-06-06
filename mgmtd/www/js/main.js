/* main.js — ChatGPT-style sidebar (expand ↔ icon-rail) */
(function () {
  'use strict';

  const COLLAPSED_KEY = 'sidebarCollapsed';

  function initSidebar() {
    const sidebar       = document.getElementById('sidebar');
    const overlay       = document.getElementById('sidebarOverlay');
    const toggleBtn     = document.getElementById('sidebarToggle');   // inside brand row
    const railToggle    = document.getElementById('railToggle');       // mobile rail header
    const railToggleBtn = document.getElementById('railToggleBtn');
    if (!sidebar) return;

    const isMobile = () => window.innerWidth <= 680;

    /* ── State helpers ── */
    function isCollapsed() { return sidebar.classList.contains('collapsed'); }

    function applyCollapsed(collapsed) {
      sidebar.classList.toggle('collapsed', collapsed);
      // Rail toggle: visible on desktop only when collapsed
      if (railToggle) {
        railToggle.style.display = (!isMobile() && collapsed) ? 'flex' : 'none';
      }
    }

    /* ── Init ── */
    if (isMobile()) {
      // Mobile: always starts hidden (slide-in overlay mode)
      sidebar.classList.remove('collapsed');
      if (railToggle) railToggle.style.display = 'flex';
    } else {
      const saved = localStorage.getItem(COLLAPSED_KEY) === 'true';
      applyCollapsed(saved);
    }

    /* ── Desktop toggle (inline button in brand row) ── */
    function collapseDesktop() {
      applyCollapsed(true);
      localStorage.setItem(COLLAPSED_KEY, 'true');
    }

    function expandDesktop() {
      applyCollapsed(false);
      localStorage.setItem(COLLAPSED_KEY, 'false');
    }

    toggleBtn?.addEventListener('click', () => {
      isCollapsed() ? expandDesktop() : collapseDesktop();
    });

    /* ── Rail toggle button (desktop collapsed / mobile hamburger) ── */
    function openMobile() {
      sidebar.classList.add('mobile-open');
      overlay?.classList.add('visible');
      if (railToggle) railToggle.style.display = 'none';
    }
    function closeMobile() {
      sidebar.classList.remove('mobile-open');
      overlay?.classList.remove('visible');
      if (railToggle) railToggle.style.display = 'flex';
    }

    railToggleBtn?.addEventListener('click', () => {
      if (isMobile()) {
        openMobile();
      } else {
        // Desktop: expand from collapsed rail
        expandDesktop();
      }
    });

    overlay?.addEventListener('click', closeMobile);

    /* ── Resize ── */
    window.addEventListener('resize', () => {
      if (!isMobile()) {
        sidebar.classList.remove('mobile-open');
        overlay?.classList.remove('visible');
        // Re-apply saved collapsed state
        const saved = localStorage.getItem(COLLAPSED_KEY) === 'true';
        applyCollapsed(saved);
      } else {
        // On mobile, sidebar is always full-width overlay
        sidebar.classList.remove('collapsed');
        applyCollapsed(false);
        if (railToggle) railToggle.style.display = 'flex';
      }
    });
  }

  function initNav() {
    const items   = document.querySelectorAll('.nav-item[data-page]');
    const current = location.pathname.split('/').pop() || 'index.html';
    items.forEach(item => {
      if (item.dataset.page === current) item.classList.add('active');
      item.addEventListener('click', () => {
        items.forEach(i => i.classList.remove('active'));
        item.classList.add('active');
      });
    });
  }

  document.addEventListener('DOMContentLoaded', () => {
    initSidebar();
    initNav();
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
    }
  };
}());
