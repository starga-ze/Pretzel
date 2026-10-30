/* staging.js — the cross-domain view of the candidate.
 *
 * Every Configuration tab registers a provider describing its own domain; this owns the question
 * none of them can answer alone — whether anything is staged, what, and what a single publish
 * would send.
 *
 *   NMS.staging.register(provider)   see the contract below
 *   NMS.staging.refresh()            recount, and repaint the Config Review button
 *   NMS.staging.publish() / revert()
 *   NMS.staging.dirtyKeys()          which domains are claiming a candidate
 *   NMS.collectChanges()             one row per entity that moved
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // ── Staging registry ─────────────────────────────────────────────────────────
  // Every tab's module is loaded on the settings page and registers a provider describing its
  // own domain. The registry then owns the cross-domain view: whether anything is dirty (drives
  // the Publish button) and what a single publish sends — regardless of which tab is showing.
  //
  //   provider = {
  //     key,                         sessionStorage draft key
  //     dirty:   () => boolean,      staged differs from the committed copy
  //     payload: () => [changes],    /api/settings/commit entries for this domain
  //     before:  () => object,       committed view, for the diff
  //     after:   () => object,       staged view, for the diff
  //     onPublished: () => void,     clear the draft, adopt staged as committed
  //     revert:  () => void,         clear the draft, put the committed copy back
  //     problems?: () => [string],   optional: references that would publish broken
  //   }
  //
  // onPublished and revert are the two ways a candidate ends, and both have to reset the module's
  // IN-MEMORY copy as well as its draft: a tab the operator never opened does not re-load, so a
  // module left holding staged objects would report dirty again on the next refresh().
  //
  // dirty() is asked of EVERY provider whenever ANY of them calls refresh(), which the other seven
  // do as each of their loads finishes. So a provider must never be observable half-loaded: if a
  // module's load() awaits anything between reading the committed copy and setting its staged one,
  // it has to hold the committed copy in a local and publish both together at the end. Two modules
  // did not, and spent that await reporting every object in their domain as deleted — which lit
  // the Config Review button on arrival and switched it off again a moment later.
  //
  // Only this page loads the registry, but the Publish button is on every page and its enabled
  // state is mirrored through sessionStorage by NMS.setPendingChanges (js/core/topbar.js) — so refresh()
  // below is what keeps that flag true wherever the operator walks next.
  const providers = [];

  const anyDirty = () => providers.some(p => p.dirty());

  function refresh() {
    // The badge counts ENTITIES, not domains: "Config Review 3" has to mean three things changed,
    // which is the number the dialog then lists. anyDirty() still decides whether anything is
    // staged at all — a provider can report dirty while collectChanges finds nothing to show
    // (a staged key with no object edit), and that must not read as a clean candidate.
    const n = collectChanges().length;
    window.NMS.setPendingChanges(n || (anyDirty() ? 1 : 0));
  }

  // ── What changed, entity by entity ───────────────────────────────────────────
  // The staging contract gives whole domains: before() and after() return objects whose values
  // are the collections a tab edits. The review dialog needs rows — one per site, device or rule
  // that actually moved — so this takes those two documents apart and pairs their members up.
  //
  // Done here rather than asked of the eight modules: the pairing rule is the same everywhere
  // (match on the stable id, fall back to the name), and eight implementations of one rule is
  // eight chances for a row to go missing from a review. A module that needs a different rule can
  // still answer `changes()` itself — see below.
  const ENTITY_LABELS = {
    sites:           'Site',
    ngfw_devices:    'NGFW Device',
    sase_devices:    'SASE Device',
    api_credentials: 'API Credential',
    endpoints:       'API Endpoint',
    connectors:      'API Connector',
    ai_providers:    'AI Provider',
    ai_route:        'AI Route',
    users:           'User Account',
    api_keys:        'Stored API Key',
    passwords:       'Account Password',
  };

  const labelFor = (key) => ENTITY_LABELS[key]
    || key.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase());

  // The stable identity of one entity, in the order the data actually carries one. `name` is last
  // and is a fallback, not a key: renaming a site would otherwise read as one removal and one
  // addition rather than the modification it is.
  const identityOf = (o, i) => (o && (o.oid || o.id || o.uuid || o.username || o.name)) || ('#' + i);
  const displayName = (o, id) =>
    (o && (o.name || o.username || o.label || o.target)) || String(id);

  const isPlainObject = (v) => !!v && typeof v === 'object' && !Array.isArray(v);
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

  // Pair up two collections and emit a row per member that moved.
  function pairRows(type, beforeVal, afterVal, meta) {
    const rows = [];
    const push = (id, b, a) => {
      if (same(b, a)) return;
      rows.push(Object.assign({
        type,
        id: String(id),
        name: displayName(a === undefined ? b : a, id),
        change: b === undefined ? 'Added' : (a === undefined ? 'Removed' : 'Modified'),
        before: b,
        after: a,
      }, meta));
    };

    if (Array.isArray(beforeVal) || Array.isArray(afterVal)) {
      const bArr = Array.isArray(beforeVal) ? beforeVal : [];
      const aArr = Array.isArray(afterVal) ? afterVal : [];
      const bMap = new Map(bArr.map((o, i) => [identityOf(o, i), o]));
      const aMap = new Map(aArr.map((o, i) => [identityOf(o, i), o]));
      // After first so the dialog lists the candidate's own order — what the operator just built,
      // in the order they built it — and removals fall in after it.
      aMap.forEach((a, id) => push(id, bMap.get(id), a));
      bMap.forEach((b, id) => { if (!aMap.has(id)) push(id, b, undefined); });
      return rows;
    }

    if (isPlainObject(beforeVal) || isPlainObject(afterVal)) {
      const b = isPlainObject(beforeVal) ? beforeVal : {};
      const a = isPlainObject(afterVal) ? afterVal : {};
      Object.keys(a).forEach(k => push(k, Object.prototype.hasOwnProperty.call(b, k) ? b[k] : undefined, a[k]));
      Object.keys(b).forEach(k => {
        if (!Object.prototype.hasOwnProperty.call(a, k)) push(k, b[k], undefined);
      });
      return rows;
    }

    // A scalar domain — nothing to take apart, so the domain itself is the row.
    push(type, beforeVal, afterVal);
    return rows;
  }

  function collectChanges() {
    const out = [];
    providers.filter(p => p.dirty()).forEach(p => {
      const meta = {
        draftKey: p.key,
        stamp: window.NMS.draft.stampOf(p.key),
        editor: window.NMS.currentUser || null,
      };

      // An escape hatch for a domain whose shape the generic pairing cannot read. Nothing uses it
      // today; it exists so that the day one does, the answer is a method on that provider rather
      // than a special case grafted into the loop above.
      if (typeof p.changes === 'function') {
        p.changes().forEach(r => out.push(Object.assign({}, meta, r)));
        return;
      }

      let before, after;
      try { before = p.before() || {}; after = p.after() || {}; }
      catch (_) { return; }   // a module mid-load cannot describe itself; the next refresh will

      const keys = new Set(Object.keys(before).concat(Object.keys(after)));
      keys.forEach(k => {
        pairRows(labelFor(k), before[k], after[k], meta).forEach(r => out.push(r));
      });
    });
    return out;
  }

  window.NMS.collectChanges = collectChanges;

  window.NMS.staging = {
    // No refresh() here: providers register before their fetch lands, so at this point every one
    // of them reports clean. Publishing that answer would clear the pending flag — and with it
    // the button state the other pages read — for as long as the load takes. Each module calls
    // refresh() itself once it has data.
    register(provider) { providers.push(provider); return provider; },
    refresh,
    anyDirty,
    // Which domains are reporting a candidate. The registry is the only thing that can answer
    // this — eight modules each know their own domain and none knows the others — so when the
    // Config Review button says something surprising, this is what says who told it to.
    dirtyKeys: () => providers.filter(p => p.dirty()).map(p => p.key),

    publish() {
      const dirty = providers.filter(p => p.dirty());
      if (!dirty.length) return;
      const payload = [];
      let problems = [];
      dirty.forEach(p => {
        payload.push.apply(payload, p.payload());
        if (typeof p.problems === 'function') problems = problems.concat(p.problems());
      });
      window.NMS.reviewFlow({
        payload, problems,
        changes: collectChanges(),
        onPublished: () => { dirty.forEach(p => p.onPublished()); refresh(); },
      });
    },

    // The other end of the candidate: drop it and go back to what the appliance is running.
    //
    // Nothing is sent. The candidate only ever existed in this browser tab — sessionStorage
    // drafts plus each module's in-memory copy — so reverting is a local act, and the running
    // configuration it reverts TO is already the thing every module re-reads from /api/settings.
    // That is also why this cannot fail halfway: there is no request to lose.
    //
    // Every dirty provider is reverted, not just the tab on screen, for the same reason publish
    // sends them all: the candidate is one document across the tabs, and leaving another tab's
    // edits staged would put the Publish button straight back on with changes the operator
    // believes they just discarded.
    revert(flowOpts) {
      const dirty = providers.filter(p => p.dirty());
      if (!dirty.length) return;

      // Checked before anything is dropped rather than discovered partway through it. A provider
      // without revert() is a programming error, but finding it on the fourth of five would leave
      // the candidate half reverted and half staged — the one state neither this flow nor the
      // Publish diff can describe, and the operator would have been told it was all discarded.
      const missing = dirty.filter(p => typeof p.revert !== 'function');
      if (missing.length) {
        window.NMS.notice(`Revert is unavailable — ${missing.map(p => p.key).join(', ')} cannot be `
                        + 'reverted. Nothing was discarded.');
        return;
      }

      window.NMS.revertFlow({
        dirty,
        onCancel: (flowOpts || {}).onCancel,
        onReverted: () => { dirty.forEach(p => p.revert()); refresh(); },
      });
    },
  };

  window.NMS.onCommit(() => window.NMS.staging.publish());
})();
