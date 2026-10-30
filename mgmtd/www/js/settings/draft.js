/* draft.js — the staged (uncommitted) configuration, as the browser holds it.
 *
 *   NMS.draft.get/set/clear(key)   per-domain drafts
 *   NMS.draft.stampOf(key)         when that draft was last written
 *   NMS.draft.checkBase(version)   drop everything if the appliance's lineage changed
 *
 * Scoped to the browser tab (sessionStorage), so it survives a reload and dies with the tab.
 * This is the store only: what counts as a change, and what is done with one, is staging.js.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // ── Staged (uncommitted) changes ─────────────────────────────────────────────
  // Settings tabs switch client-side (see core/topbar.js), but staged state is still held in
  // sessionStorage so it survives a full page reload too: scoped to this browser tab, gone when
  // the tab closes. Publishing clears it; the server copy then wins.
  const DRAFT_PREFIX = 'pz.draft.';
  const BASE_KEY = DRAFT_PREFIX + '__base_version';
  const STAMP_KEY = DRAFT_PREFIX + '__staged_at';

  // When each draft was last written, for the review dialog's "Last update" column.
  //
  // Recorded here and not in the eight modules that stage, because set() is the one funnel every
  // one of them already goes through — a module that forgets to stamp is a column that is quietly
  // wrong, and there is no reason to give anyone the chance. Per draft key rather than per entity:
  // that is the granularity the staging contract has, and inventing a per-row timestamp we never
  // observed would be worse than showing the one we did.
  function stampNow(key) {
    try {
      const all = JSON.parse(sessionStorage.getItem(STAMP_KEY) || '{}');
      all[key] = new Date().toISOString();
      sessionStorage.setItem(STAMP_KEY, JSON.stringify(all));
    } catch (_) { /* quota/private mode — the column falls back to a dash */ }
  }

  window.NMS.draft = {
    get(key, fallback) {
      try {
        const raw = sessionStorage.getItem(DRAFT_PREFIX + key);
        return raw === null ? fallback : JSON.parse(raw);
      } catch (_) { return fallback; }
    },
    set(key, value) {
      try { sessionStorage.setItem(DRAFT_PREFIX + key, JSON.stringify(value)); } catch (_) { /* quota/private mode */ }
      stampNow(key);
    },
    stampOf(key) {
      try { return (JSON.parse(sessionStorage.getItem(STAMP_KEY) || '{}'))[key] || null; }
      catch (_) { return null; }
    },
    clear(key) {
      try {
        sessionStorage.removeItem(DRAFT_PREFIX + key);
        const all = JSON.parse(sessionStorage.getItem(STAMP_KEY) || '{}');
        delete all[key];
        sessionStorage.setItem(STAMP_KEY, JSON.stringify(all));
      } catch (_) { /* ignore */ }
    },

    // Staged edits are per browser tab and survive a page reload — which means they also survive
    // the server losing the configuration they were made against. After `pretzel reset` (or a
    // rollback) the version goes backwards and every staged oid refers to objects that no longer
    // exist, so the drafts are dropped rather than offered for publish.
    //
    // Called by each module right after it fetches /api/settings and before it reads its draft,
    // so whichever module loads first does the clearing.
    checkBase(version) {
      if (typeof version !== 'number' || version <= 0) return;
      let base = 0;
      try { base = parseInt(sessionStorage.getItem(BASE_KEY), 10) || 0; } catch (_) { return; }

      if (base && version < base) {
        try {
          Object.keys(sessionStorage)
            .filter(k => k.indexOf(DRAFT_PREFIX) === 0 && k !== BASE_KEY)
            .forEach(k => sessionStorage.removeItem(k));
        } catch (_) { /* ignore */ }
        // Nothing is staged any more, and the flag is what every other page reads.
        window.NMS.setPendingChanges(0);
        staleBase = { from: base, to: version };
      }

      try { sessionStorage.setItem(BASE_KEY, String(version)); } catch (_) { /* ignore */ }
    },
  };

  // Set when checkBase discarded drafts, so the page can say so once it has rendered.
  let staleBase = null;


  // Report a discard once, after the tab has painted — an alert during module load would fire
  // before the operator can see what changed.
  document.addEventListener('DOMContentLoaded', () => {
    setTimeout(() => {
      if (!staleBase) return;
      const { from, to } = staleBase;
      staleBase = null;
      // Two paragraphs rather than a \n\n: the dialog escapes its text into a <p>, where a newline
      // is whitespace. `detail` is the second paragraph the notice already renders.
      window.NMS.notice({
        title: 'Staged changes discarded',
        message: 'Staged changes were discarded. Nothing was published.',
        detail: `They were made against configuration version ${from}, but the appliance is now on `
              + `version ${to} — the configuration they referred to no longer exists `
              + `(a reset or a rollback).`,
      });
    }, 0);
  });
})();
