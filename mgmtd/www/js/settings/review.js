/* review.js — reading the configuration, and committing it.
 *
 *   NMS.viewRunningConfig()   the committed document in full, read-only
 *   NMS.reviewFlow(opts)      the review dialog, and the publish behind it
 *
 * The dialog lists the entities that moved and opens each one's diff in place; publishing hands
 * the batch to /api/settings/commit and then watches the fleet converge onto the new version.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;

  const RELOAD_TIMEOUT_MS = 30000;
  const POLL_MS = 800;

  // ── Running-config viewer ────────────────────────────────────────────────────
  // The committed document in full, as the daemons see it — the counterpart to Publish's diff,
  // which only shows what is about to change. Read-only, so it reuses the modal without a foot
  // action beyond Close.
  window.NMS.viewRunningConfig = async function () {
    const ov = window.NMS.modal.ensure();
    ov.querySelector('#cmTitle').textContent = 'Running configuration';
    ov.querySelector('#cmBody').innerHTML = `<div class="cm-loading">Loading…</div>`;
    ov.querySelector('#cmFoot').innerHTML = `<button class="btn-sm" id="cmDone">Close</button>`;
    ov.querySelector('#cmDone').onclick = window.NMS.modal.close;
    ov.classList.add('open');

    let doc;
    try {
      const r = await fetch('/api/settings/running-config',
                            { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      if (!r.ok) throw new Error('HTTP ' + r.status);
      doc = await r.json();
    } catch (e) {
      ov.querySelector('#cmBody').innerHTML = `<div class="cm-loading">Failed to load: ${esc(e.message)}</div>`;
      return;
    }

    const pretty = JSON.stringify(doc.config, null, 2);
    ov.querySelector('#cmTitle').textContent = `Running configuration — version ${esc(doc.version)}`;
    ov.querySelector('#cmBody').innerHTML = `
      <div class="rc-meta">Committed ${esc(doc.committed_at || '')}${
        window.NMS.staging.anyDirty() ? ' · <b class="rc-dirty">staged changes are not included</b>' : ''}</div>
      <pre class="rc-doc">${esc(pretty)}</pre>`;
    ov.querySelector('#cmFoot').innerHTML = `
      <button class="btn-sm" id="cmCopy">Copy JSON</button>
      <span style="flex:1"></span>
      <button class="btn-sm" id="cmDone">Close</button>`;
    ov.querySelector('#cmDone').onclick = window.NMS.modal.close;
    ov.querySelector('#cmCopy').onclick = (e) => {
      navigator.clipboard?.writeText(pretty)
        .then(() => { e.target.textContent = 'Copied'; })
        .catch(() => { e.target.textContent = 'Copy failed'; });
    };
  };


  function progView() {
    return `
      <div class="cm-prog">
        <div class="cm-prog-head"><span id="cmProgLabel">Publishing</span><span id="cmProgPct">0%</span></div>
        <div class="cm-prog-track"><div class="cm-prog-fill" id="cmProgFill"></div></div>
      </div>`;
  }
  function setProg(pct, label, kind) {
    const f = document.getElementById('cmProgFill'), p = document.getElementById('cmProgPct'), l = document.getElementById('cmProgLabel');
    if (f) { f.style.width = Math.round(pct) + '%'; f.className = 'cm-prog-fill' + (kind ? ' ' + kind : ''); }
    if (p) p.textContent = Math.round(pct) + '%';
    if (l && label) l.textContent = label;
  }

  async function pollProgress(startTs) {
    if (!document.getElementById('cmProgFill')) return;
    let st;
    try {
      st = await fetch('/api/settings/reload-status', { credentials: 'same-origin', headers: { Accept: 'application/json' } })
        .then(r => r.json());
    } catch (_) { st = null; }

    const elapsed = Date.now() - startTs;
    const foot = document.getElementById('cmFoot');
    if (st && st.status === 'complete') {
      setProg(100, 'Published', 'ok');

      // Re-read the page behind the modal, exactly as the refresh button does. Until now the
      // operator had to press it themselves: a published site appeared in the list, or a deleted
      // one left it, only on the next manual refresh — so the screen that had just been changed was
      // the one screen still showing the old answer. Convergence is the right moment for it, not
      // the click on Done: by the time the modal is closed the page underneath is already current.
      //
      // The page's own handler, so each one reloads the way it knows how; every settings module's
      // reload goes through NMS.utils.loadSettings, which is also what refills the sidebar's site
      // switcher. No fallback to location.reload() — that would tear down this modal mid-sentence.
      if (typeof window.NMS._onRefresh === 'function') {
        try { window.NMS._onRefresh(); } catch (_) { /* a page that cannot reload still published */ }
      }

      foot.innerHTML = `<button class="btn-primary btn-sm" id="cmDone">Done</button>`;
      document.getElementById('cmDone').onclick = window.NMS.modal.close;
      return;
    }
    // The configuration was written but the fleet did not converge onto it — a daemon failed to come
    // back. Distinct from a timeout, which says only that it is taking longer than we wait: this is
    // engined reporting that it is not coming. Nothing here can be retried by publishing again, so
    // the message points at the logs rather than offering a button that would not help.
    if (st && st.status === 'failed') {
      setProg(100, 'Applied, but the daemons did not come back', 'error');
      document.getElementById('cmBody')?.insertAdjacentHTML('beforeend', `<div class="cm-warn">
          <div class="cm-warn-h">Reload did not converge</div>
          <ul><li>The new configuration was committed, but at least one daemon failed to restart
            onto it. The appliance is running a mix of old and new configuration.</li></ul>
          <div class="cm-warn-note">Check <a href="monitor?tab=system-log">Monitor › System Log</a>
            for the daemon that did not report ready. Publishing again will not fix this.</div>
        </div>`);
      foot.innerHTML = `<button class="btn-sm" id="cmDone">Close</button>`;
      document.getElementById('cmDone').onclick = window.NMS.modal.close;
      return;
    }
    if (elapsed >= RELOAD_TIMEOUT_MS) {
      setProg(100, 'Timed out — committed, still applying', 'warn');
      foot.innerHTML = `<button class="btn-sm" id="cmDone">Close</button>`;
      document.getElementById('cmDone').onclick = window.NMS.modal.close;
      return;
    }
    const cur = parseInt(document.getElementById('cmProgFill').style.width) || 30;
    setProg(cur + (90 - cur) * 0.25, 'Applying');
    setTimeout(() => pollProgress(startTs), POLL_MS);
  }

  async function doCommit(opts) {
    const body = document.getElementById('cmBody'), foot = document.getElementById('cmFoot');
    foot.innerHTML = `<button class="btn-sm" id="cmDone" disabled>Working…</button>`;
    body.innerHTML = progView();
    setProg(10, 'Publishing');

    try {
      const r = await fetch('/api/settings/commit', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ changes: opts.payload }),
      });
      const res = await r.json().catch(() => ({}));

      // `failed` has to be read even on a 2xx, and the body has to be read even on a non-2xx. A
      // partially accepted batch used to answer 200 with the rejections buried in `results`, and
      // this only looked at `applied` — so onPublished below cleared the drafts for changes the
      // server never took. That was the one way a published edit could vanish without a word.
      const rejected = (res.results || []).filter(x => x && x.ok === false);
      if (!r.ok || (res.failed | 0) > 0 || (res.applied | 0) <= 0) {
        throw Object.assign(new Error(res.error || 'HTTP ' + r.status), { rejected });
      }
    } catch (e) {
      setProg(100, 'Not published', 'error');
      body.insertAdjacentHTML('beforeend', rejectionBlock(e.message, e.rejected));
      foot.innerHTML = `<button class="btn-sm" id="cmDone">Close</button>`;
      document.getElementById('cmDone').onclick = window.NMS.modal.close;
      return;
    }

    if (typeof opts.onPublished === 'function') opts.onPublished();
    setProg(30, 'Applying');
    pollProgress(Date.now());
  }

  // Why nothing was published. The server answers per change, and those reasons are the useful part
  // — "connector collects an endpoint that does not exist" names what to go and fix, where a bare
  // status code sends the operator to the logs. The closing line matters as much: the drafts are
  // still staged, so this is a retry rather than lost work.
  function rejectionBlock(message, rejected) {
    const items = (rejected && rejected.length)
      ? rejected.map(x => `<li><b>${esc(x.daemon)}.${esc(x.domain)}</b> — ${esc(x.error || 'rejected')}</li>`).join('')
      : `<li>${esc(message)}</li>`;
    return `<div class="cm-warn">
        <div class="cm-warn-h">Nothing was published</div>
        <ul>${items}</ul>
        <div class="cm-warn-note">The batch is applied whole or not at all, so no part of it was
          committed. Your staged changes are still here — fix the above and publish again.</div>
      </div>`;
  }

  // Dangling references are worth stopping on: they publish quietly and only show up later as a
  // red "missing" in a table. The commonest cause is staged edits outliving the configuration
  // they were made against — a server reset or rollback leaves drafts pointing at objects that
  // no longer exist, since staging is per browser tab and survives both.
  function problemBlock(problems) {
    if (!problems || !problems.length) return '';
    return `<div class="cm-warn">
        <div class="cm-warn-h">${problems.length} broken reference${problems.length > 1 ? 's' : ''}</div>
        <ul>${problems.map(t => `<li>${esc(t)}</li>`).join('')}</ul>
        <div class="cm-warn-note">Publishing anyway is allowed, but these will show as
          <b>missing</b> until the target exists. If the staged edits predate a reset, cancel and
          recreate what they point at — or close this browser tab, which discards them.</div>
      </div>`;
  }


  // ── Review and publish ───────────────────────────────────────────────────────
  // The candidate, laid out as what it is: a list of entities that moved, each openable to the
  // lines that moved in it. The old flow showed one diff of every domain concatenated, which
  // answered "is anything different" and not "what did I change", and on a busy candidate the
  // thing the operator was looking for was somewhere in four hundred lines.
  //
  // Nothing here sends anything. Publish hands the same payload to doCommit that the old flow
  // did, so the commit, its rejection reporting and the convergence poll are untouched.
  window.NMS.reviewFlow = function (opts) {
    const changes = opts.changes || [];

    // Said out loud rather than returned from silently. A provider whose dirty() is true while
    // its before()/after() describe no entity is the one failure this flow cannot render — the
    // operator would press Publish, watch a dialog refuse to open, and have no way to tell that
    // from a dead button. It is also the exact bug the key-state views exist to prevent, so the
    // message names where to look.
    if (!changes.length) {
      window.NMS.notice({
        title: 'Nothing to review',
        message: 'Changes are staged, but none of them appear in the configuration diff, so there '
               + 'is nothing to publish.',
        detail: 'A tab is reporting staged changes that its before/after view does not describe. '
              + 'Reverting will clear them.',
      });
      return;
    }

    const ov = window.NMS.modal.ensure();
    ov.querySelector('.cm-dialog').classList.add('cm-dialog-wide');
    ov.querySelector('#cmTitle').textContent = 'Review and publish changes';
    ov.classList.add('open');

    const state = { pane: 'changes', q: '', type: 'all', change: 'all', open: {} };
    // Filled from the appliance below; the dialog opens on what it already knows so the list is
    // readable before the fetch lands, and the version line fills in when it does.
    let published = { version: null, committed_at: null };

    const types = Array.from(new Set(changes.map(c => c.type))).sort();
    const kinds = ['Added', 'Modified', 'Removed'].filter(k => changes.some(c => c.change === k));

    const visible = () => changes.filter(c => {
      if (state.type !== 'all' && c.type !== state.type) return false;
      if (state.change !== 'all' && c.change !== state.change) return false;
      if (!state.q) return true;
      const q = state.q.toLowerCase();
      return c.name.toLowerCase().indexOf(q) !== -1 || c.type.toLowerCase().indexOf(q) !== -1;
    });

    const rowId = (c) => `${c.draftKey}::${c.type}::${c.id}`;

    function rowsHtml() {
      const list = visible();
      if (!list.length) {
        return `<div class="rv-empty">${changes.length
          ? 'No change matches these filters.'
          : 'Nothing is staged.'}</div>`;
      }
      return list.map(c => {
        const id = rowId(c);
        const isOpen = !!state.open[id];
        const heads = {
          left: published.committed_at
            ? `Last published (${window.NMS.diff.fmtStamp(published.committed_at)})` : 'Last published',
          right: c.stamp ? `Draft (${window.NMS.diff.fmtStamp(c.stamp)})` : 'Draft',
        };
        return `<div class="rv-row${isOpen ? ' is-open' : ''}" data-row="${esc(id)}">
            <div class="rv-cells">
              <div class="rv-c rv-c-type">
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                  <path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>
                </svg>${esc(c.type)}</div>
              <div class="rv-c rv-c-name" title="${esc(c.name)}">${esc(c.name)}</div>
              <div class="rv-c rv-c-change">
                <span class="rv-chip rv-${c.change.toLowerCase()}">${esc(c.change)}</span></div>
              <div class="rv-c rv-c-when">
                ${c.stamp ? esc(window.NMS.diff.fmtStamp(c.stamp)) : '<span class="rv-dash">—</span>'}
                ${c.editor ? `<i class="rv-editor">${esc(c.editor)}</i>` : ''}
              </div>
              <div class="rv-c rv-c-act">
                <button class="rv-diff-btn" type="button" data-toggle="${esc(id)}">
                  <span class="rv-diff-btn-t">${isOpen ? 'Hide diff' : 'Show diff'}</span>
                  <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                       stroke-width="3"><polyline points="6 9 12 15 18 9"/></svg>
                </button>
              </div>
            </div>
            ${isOpen ? window.NMS.diff.entityHtml(c.before, c.after, heads) : ''}
          </div>`;
      }).join('');
    }

    function versionHtml() {
      const cur = published.version;
      const next = typeof cur === 'number' ? cur + 1 : null;
      const line = (k, v) => `<div class="rv-kv"><span>${esc(k)}</span><b>${v}</b></div>`;
      return `<h3 class="rv-h">Version details</h3>
        <p class="cm-lead">A publish mints a new configuration version. The daemons converge onto
          it; the version before it stays readable as the one they were running.</p>
        <div class="rv-kvs">
          ${line('Running version', cur == null ? '—' : '#' + cur)}
          ${line('Version after publish', next == null ? '—' : '#' + next)}
          ${line('Last published', published.committed_at ? esc(window.NMS.diff.fmtStamp(published.committed_at)) : '—')}
          ${line('Changes in this publish', String(changes.length))}
          ${line('Editor', esc(window.NMS.currentUser || '—'))}
        </div>`;
    }

    function paint() {
      const body = ov.querySelector('#cmBody');
      const n = changes.length;
      const list = visible();

      body.innerHTML = `
        <div class="rv">
          <aside class="rv-nav">
            <button class="rv-nav-item${state.pane === 'changes' ? ' active' : ''}" type="button" data-pane="changes">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M9 11l3 3L22 4"/><path d="M21 12v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11"/>
              </svg>
              <span><b>Review changes</b><i>${n} change${n === 1 ? '' : 's'}</i></span>
            </button>
            <button class="rv-nav-item${state.pane === 'version' ? ' active' : ''}" type="button" data-pane="version">
              <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>
              </svg>
              <span><b>Version details</b><i>${published.version == null
                ? 'Loading…' : 'Version #' + (published.version + 1)}</i></span>
            </button>
          </aside>
          <section class="rv-main">${state.pane === 'version' ? versionHtml() : `
            <h3 class="rv-h">Review changes</h3>
            ${problemBlock(opts.problems)}
            <div class="rv-tools">
              <label class="rv-search">
                <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2">
                  <circle cx="11" cy="11" r="7"/><line x1="21" y1="21" x2="16.65" y2="16.65"/>
                </svg>
                <input id="rvSearch" type="search" placeholder="Search by name"
                       value="${esc(state.q)}" autocomplete="off">
              </label>
              <span class="rv-sel-wrap"><select class="rv-sel" id="rvType" aria-label="Entity type">
                <option value="all">Entity type: All</option>
                ${types.map(t => `<option value="${esc(t)}"${state.type === t ? ' selected' : ''}>Entity type: ${esc(t)}</option>`).join('')}
              </select></span>
              <span class="rv-sel-wrap"><select class="rv-sel" id="rvChange" aria-label="Change type">
                <option value="all">Change type: All</option>
                ${kinds.map(k => `<option value="${esc(k)}"${state.change === k ? ' selected' : ''}>Change type: ${esc(k)}</option>`).join('')}
              </select></span>
              <span class="rv-count">${list.length ? `1–${list.length}` : '0'} of ${n}</span>
            </div>
            <div class="rv-head">
              <div class="rv-c rv-c-type">Entity type</div>
              <div class="rv-c rv-c-name">Name</div>
              <div class="rv-c rv-c-change">Change type</div>
              <div class="rv-c rv-c-when">Last update</div>
              <div class="rv-c rv-c-act"></div>
            </div>
            <div class="rv-rows">${rowsHtml()}</div>`}
          </section>
        </div>`;

      wireBody();
    }

    function wireBody() {
      const body = ov.querySelector('#cmBody');

      body.querySelectorAll('[data-pane]').forEach(b => b.addEventListener('click', () => {
        state.pane = b.dataset.pane;
        paint();
      }));

      const search = body.querySelector('#rvSearch');
      if (search) search.addEventListener('input', () => { state.q = search.value; refreshList(); });

      // The list only, never the whole pane. Repainting would rebuild the two selects underneath
      // the themed dropdown that just dispatched this event — its trigger and its body-mounted
      // panel are built around the live <select>, and tearing that out mid-`choose()` leaves the
      // focus on a detached node. It also kept the caret out of the search box on every keystroke.
      body.querySelector('#rvType')?.addEventListener('change', (e) => {
        state.type = e.target.value; refreshList();
      });
      body.querySelector('#rvChange')?.addEventListener('change', (e) => {
        state.change = e.target.value; refreshList();
      });

      // Native <select> option lists are drawn by the operating system and cannot be styled, so
      // the console replaces them everywhere — this dialog included, or it would be the one
      // surface wearing Windows' dropdown on a Windows box and GTK's on this one. The <select>
      // stays as the value store, which is why the listeners above are attached first and keep
      // working afterwards.
      window.NMS.utils?.enhanceSelects?.(body);

      wireRows();
    }

    // The rows and the count, in place.
    function refreshList() {
      const body = ov.querySelector('#cmBody');
      const rows = body.querySelector('.rv-rows');
      if (rows) { rows.innerHTML = rowsHtml(); wireRows(); }
      const count = body.querySelector('.rv-count');
      if (count) {
        const v = visible().length;
        count.textContent = `${v ? '1–' + v : '0'} of ${changes.length}`;
      }
    }

    function wireRows() {
      const body = ov.querySelector('#cmBody');

      body.querySelectorAll('[data-toggle]').forEach(b => b.addEventListener('click', () => {
        const id = b.dataset.toggle;
        state.open[id] = !state.open[id];
        // Only this row is rebuilt: repainting the list would collapse every other open diff and
        // throw away the scroll position, which on a long review is the operator's place in it.
        const row = body.querySelector(`[data-row="${CSS.escape(id)}"]`);
        const c = changes.find(x => rowId(x) === id);
        if (!row || !c) return;
        const heads = {
          left: published.committed_at
            ? `Last published (${window.NMS.diff.fmtStamp(published.committed_at)})` : 'Last published',
          right: c.stamp ? `Draft (${window.NMS.diff.fmtStamp(c.stamp)})` : 'Draft',
        };
        row.classList.toggle('is-open', !!state.open[id]);
        row.querySelector('.rv-diff')?.remove();
        const label = b.querySelector('.rv-diff-btn-t');
        if (label) label.textContent = state.open[id] ? 'Hide diff' : 'Show diff';
        if (state.open[id]) row.insertAdjacentHTML('beforeend', window.NMS.diff.entityHtml(c.before, c.after, heads));
        wireFolds(row);
      }));

      body.querySelectorAll('.rv-row.is-open').forEach(wireFolds);
    }

    function wireFolds(scope) {
      scope.querySelectorAll('[data-fold]').forEach(b => {
        if (b.dataset.wired) return;
        b.dataset.wired = '1';
        b.addEventListener('click', () => {
          b.nextElementSibling?.classList.remove('is-folded');
          b.remove();
        });
      });
    }

    function foot() {
      const n = changes.length;
      ov.querySelector('#cmFoot').innerHTML = `
        <button class="btn-sm btn-danger rv-discard" id="rvDiscard" type="button">
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4">
            <polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/>
          </svg>
          Discard all changes (${n})
        </button>
        <span style="flex:1"></span>
        <button class="btn-sm" id="cmCancel" type="button">Cancel</button>
        <button class="btn-primary btn-sm" id="cmPublish" type="button">Publish</button>`;

      ov.querySelector('#cmCancel').onclick = window.NMS.modal.close;
      ov.querySelector('#cmPublish').onclick = () => {
        // Back to the ordinary width before the progress bar replaces the list: a 12px bar
        // centred in 1180px of empty dialog reads as something having gone wrong.
        ov.querySelector('.cm-dialog').classList.remove('cm-dialog-wide');
        doCommit(opts);
      };
      // Discard leaves this dialog and opens the revert confirmation rather than discarding
      // where it stands: throwing the candidate away is the one irreversible thing on this
      // screen, and it is not going to happen on a single click next to Cancel. Cancelling that
      // confirmation comes back here, so a mis-click costs the operator nothing.
      ov.querySelector('#rvDiscard').onclick = () =>
        window.NMS.staging.revert({ onCancel: () => window.NMS.reviewFlow(opts) });
    }

    paint();
    foot();

    // The published version and its timestamp, for the diff headers and the version pane. Asked
    // for after the dialog is up: it is a label on work the operator can already read, so making
    // them wait on a fetch to see their own changes would be the wrong way round.
    fetch('/api/settings/running-config', { credentials: 'same-origin', headers: { Accept: 'application/json' } })
      .then(r => (r.ok ? r.json() : null))
      .then(d => {
        if (!d || !ov.classList.contains('open')) return;
        published = { version: Number(d.version), committed_at: d.committed_at || null };
        // Repainting would close any diff opened while the fetch was in flight, so only the two
        // places that name the version are rewritten.
        const sub = ov.querySelector('.rv-nav-item[data-pane="version"] i');
        if (sub) sub.textContent = 'Version #' + (published.version + 1);
        if (state.pane === 'version') paint();
      })
      .catch(() => { /* the headers fall back to their unlabelled form */ });
  };
})();
