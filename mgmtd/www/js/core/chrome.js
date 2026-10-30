/* chrome.js — the two things every page wears: the hover tooltip and the theme.
 *
 *   NMS.chrome.initTooltips() / initTheme()
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

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

  // ── Theme ─────────────────────────────────────────────────────────────────
  // One attribute on <html> selects the palette; main.css does the rest (see the :root[data-theme]
  // block there). The choice is stamped before first paint by the inline script in each page, so
  // this module only has to keep the footer control in sync and write the preference down.
  //
  // A stored choice is always explicit ('dark' or 'light'), never "follow the system": an operator
  // who picked light on a machine set to dark meant it, and should not have it undone at sunset.
  // Only the absence of a choice follows the system.

  const THEME_KEY = 'theme';

  const MOON = `<svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
      <path d="M20.5 14.4A8.5 8.5 0 1 1 9.6 3.5a6.8 6.8 0 0 0 10.9 10.9z"/>
    </svg>`;

  const systemTheme = () =>
    (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) ? 'dark' : 'light';

  function storedTheme() {
    try {
      const v = localStorage.getItem(THEME_KEY);
      return (v === 'dark' || v === 'light') ? v : null;
    } catch (_) { return null; }
  }

  function applyTheme(theme, persist) {
    const t = theme === 'dark' ? 'dark' : 'light';
    document.documentElement.setAttribute('data-theme', t);
    if (persist) { try { localStorage.setItem(THEME_KEY, t); } catch (_) { /* private mode */ } }
    syncThemeControl(t);
  }

  // One switch with a fixed name: "Dark mode", on or off. Swapping the label between Dark and Light
  // made the row read as two different controls depending on when you looked at it.
  function syncThemeControl(t) {
    const btn = document.getElementById('navTheme');
    const ic = document.getElementById('navThemeIc');
    if (!btn || !ic) return;
    const dark = t === 'dark';
    btn.classList.toggle('on', dark);
    btn.setAttribute('aria-pressed', dark ? 'true' : 'false');
    btn.setAttribute('data-tooltip', dark ? 'Dark mode · on' : 'Dark mode · off');
    if (!ic.firstChild) ic.innerHTML = MOON;
  }

  function initTheme() {
    const stored = storedTheme();
    applyTheme(stored || systemTheme(), false);

    document.getElementById('navTheme')?.addEventListener('click', () => {
      const now = document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
      applyTheme(now === 'dark' ? 'light' : 'dark', true);
    });

    // With no explicit choice on record, follow the machine when it changes.
    const mq = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)');
    if (mq && mq.addEventListener) {
      mq.addEventListener('change', (e) => {
        if (!storedTheme()) applyTheme(e.matches ? 'dark' : 'light', false);
      });
    }
  }

  window.NMS.chrome = { initTooltips, initTheme };
})();
