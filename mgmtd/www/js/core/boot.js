/* boot.js — the order the console comes up in.
 *
 * Last script on every page. Everything above it only defines things; this is what runs them,
 * and the sequence is the file's whole content on purpose — when something paints before the
 * thing it depends on, this is the one place that says why.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // ── Init ──────────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    window.NMS.nav.build();
    // Paints the remembered sidebar counts — straight after the markup that carries them, and
    // NOT from window.NMS.topbar.build(), which is where this first lived. That function bails early for a
    // page with no topbar (`bareTopbar`, and any page absent from PAGES), so on the Assistant the
    // badge was never painted at all: the sidebar is on every page, the topbar is not.
    //
    // A count as of the last visit, not a live one — which is why the badge disappears at zero
    // rather than showing a confident "0", and why nothing paints until a page has counted.
    window.NMS.navBadge.paintAll();
    window.NMS.topbar.build();
    window.NMS.nav.initSidebar();
    window.NMS.nav.initTiers();
    window.NMS.nav.initFlyouts();
    window.NMS.chrome.initTooltips();
    window.NMS.chrome.initTheme();
    window.NMS.session.initUserFooter();
  });
})();
