/* session.js — who is signed in, and for how long.
 *
 * Self-starting: the guard and the heartbeat run on load, because a page that renders before it
 * knows the session is still valid has already shown the previous person's data.
 *
 *   NMS.session.initUserFooter()  identity + logout in the sidebar foot
 *   NMS.clearOnLogout(key)        a localStorage key that must not outlive the session
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // ── Session guard ─────────────────────────────────────────────────────────
  // Any API call that comes back 401 with the UNAUTHENTICATED marker means the session expired
  // (30-min TTL) or the server was restarted — mgmtd keeps sessions in memory, so a `./pretzel
  // start` redeploy drops them all and the old cookie is dead. Whatever the operator was doing,
  // bounce them to the login page. Installed here, at the top of the first script every
  // authenticated page loads, so EVERY fetch is covered — not just the ones that remembered to
  // check r.status. Semantic 401s (wrong password on the login/change-password forms) carry no
  // such code and pass straight through to their own handlers. The login page does not load
  // core/session.js, so this never causes a redirect loop.
  (function installSessionGuard() {
    const origFetch = window.fetch.bind(window);
    let redirecting = false;
    window.fetch = async function (...args) {
      const res = await origFetch(...args);
      if (res.status === 401 && !redirecting) {
        try {
          const data = await res.clone().json();
          if (data && data.code === 'UNAUTHENTICATED') {
            redirecting = true;
            window.location.href = '/';
          }
        } catch (e) { /* non-JSON 401 — leave it to the caller */ }
      }
      return res;
    };
  })();

  // ── Session heartbeat ─────────────────────────────────────────────────────
  // Two jobs, and they must stay separate — the whole idle timeout depends on the distinction.
  //
  //   renew  POST /api/session/keepalive — the only route that moves the session's expiry.
  //          Fired from real operator input (pointer, key, wheel) and nothing else, throttled to
  //          one per RENEW_MS. So the 30-minute TTL measures idleness, not how long ago the
  //          operator signed in.
  //   probe  GET /api/whoami — validates without extending. This is what notices a session that
  //          died while the tab sat idle, so the operator meets the login page rather than a
  //          screen full of stale data.
  //
  // Nothing else may renew: the Home dashboard, the Topology and API Collection live views
  // and the log tail all poll on timers, and if polling extended the session then leaving any of
  // those open would keep it alive for ever. That is also why returning to the tab only probes —
  // alt-tabbing back is not work, and must not buy another 30 minutes.
  //
  // The redirect itself is still the 401 guard above; here we only make sure a request happens.
  (function installSessionHeartbeat() {
    const RENEW_MS = 30000;   // at most one renewal per 30s of continuous activity
    const PROBE_MS = 60000;   // idle liveness check
    const PROBE_MIN_MS = 3000;

    let renewedAt = 0, probedAt = 0;

    // Renewing is itself an authenticated call, so an operator who comes back to a dead session
    // and clicks is bounced by the 401 on this very request — no separate check needed.
    function noteActivity() {
      const now = Date.now();
      if (now - renewedAt < RENEW_MS) return;
      renewedAt = now;
      probedAt = now;
      window.fetch('/api/session/keepalive', { method: 'POST', credentials: 'same-origin' })
        .catch(() => {});
    }

    function probe() {
      const now = Date.now();
      if (now - probedAt < PROBE_MIN_MS) return;
      probedAt = now;
      window.fetch('/api/whoami', { credentials: 'same-origin' }).catch(() => {});
    }

    ['pointerdown', 'keydown', 'wheel'].forEach(
      t => document.addEventListener(t, noteActivity, { capture: true, passive: true }));

    setInterval(probe, PROBE_MS);
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') probe();
    });
    window.addEventListener('focus', probe);
  })();


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
        // Published for the review dialog's "Last editor" column. The drafts it lists belong to
        // this browser tab and this session, so whoever is signed in now is who staged them —
        // there is no second editor a draft could have come from.
        if (info && info.username) window.NMS.currentUser = info.username;
      })
      .catch(() => { if (nameEl) nameEl.textContent = '—'; });

    document.getElementById('navLogout')?.addEventListener('click', async () => {
      // Cleared before the request, not after: the redirect below may win the race with a slow
      // POST, and a logout that ends the server session while leaving the person's chat threads
      // in this browser is the failure worth avoiding. Clearing twice would be harmless; not
      // clearing at all is what leaves one user's conversations for the next one to read.
      for (const key of window.NMS._userStoreKeys || []) {
        try { localStorage.removeItem(key); } catch (_) { /* private mode — nothing stored anyway */ }
      }
      try {
        await fetch('/api/logout', { method: 'POST', credentials: 'same-origin' });
      } catch (_) { /* logout is best-effort; redirect regardless */ }
      window.location.href = '/';
    });
  }


  // localStorage keys that belong to the signed-in person and must not outlive their session.
  // Logging out ends a session on the server; anything a page kept in this browser is still here
  // afterwards, and the next person to sign in on this machine sees it. Registered by name rather
  // than cleared wholesale so a page's genuinely device-level preferences (theme, rail width) are
  // not thrown away with the data.
  window.NMS._userStoreKeys = [];
  window.NMS.clearOnLogout = function (key) {
    if (key && !window.NMS._userStoreKeys.includes(key)) window.NMS._userStoreKeys.push(key);
  };

  // Only one custom-select dropdown is open at a time; opening a second closes the first.
  let csActiveClose = null;

  window.NMS.session = { initUserFooter };
})();
