/* techdoc.js — Configuration ▸ System Management ▸ Operation ▸ Tech Documentation.
 *
 * The assistant answers out of a corpus crawled from docs.paloaltonetworks.com. This card is the
 * control for it: what is stored, when it was collected, and a button to collect it again.
 *
 * Update re-fetches every page the sitemap lists — there is no incremental path, so there is
 * nothing to check first and no list of pending changes to approve. What it discards on the way
 * (pages Palo Alto has removed, URLs that redirect onto a page already stored, bodies that render
 * to nothing) is counted but not itemised: an operator can do nothing about a page that no longer
 * exists, and listing two hundred of them reads as breakage rather than as housekeeping.
 *
 * View opens the corpus browser at /tech-doc, which is where the collected documents are
 * actually inspected.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};
  const { esc } = window.NMS.utils;
  const IC = window.NMS.utils.icons;

  const POLL_MS = 1000;
  const TICKET_MS = 400;
  const TICKET_TRIES = 90;
  // The run belongs to the appliance, not to this window: mgmtd holds the stream and keeps the
  // progress in a slot on the server, and this page only polls it. Closing the tab, logging out
  // or opening the console somewhere else changes nothing about the crawl - reopening this card
  // re-attaches to it. Two earlier versions of this line said the opposite ("not performed
  // asynchronously", then "closing the page stops it") and both were wrong; the only thing that
  // stops a run is the Cancel button.
  const WARNING = 'This runs on the appliance and keeps going if you close this window. '
                + 'Use Cancel to stop it.';

  let status = null;   // CorpusStatus
  let prog = null;     // RefreshProgress + {running, idle}
  let timer = null;
  let polling = false; // a poll chain is alive; see stopPolling
  let step = null;     // null | 'confirm' | 'running' | 'ended'
  let note = null;

  // protobuf's JSON mapping renders int64 as a *string*, not a number.
  const num = (n) => {
    if (n === null || n === undefined || n === '') return '—';
    const v = typeof n === 'number' ? n : Number(n);
    return Number.isFinite(v) ? v.toLocaleString() : '—';
  };

  const api = (url, opts) =>
    fetch(url, Object.assign({ credentials: 'same-origin',
                               headers: { Accept: 'application/json' } }, opts || {}))
      .then(r => (r.status === 401 ? (location.href = '/', null) : r))
      .catch(() => null);

  const post = (url, body) =>
    api(url, { method: 'POST',
               headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
               body: JSON.stringify(body || {}) });

  async function awaitTicket(ticket) {
    for (let i = 0; i < TICKET_TRIES; i++) {
      await new Promise(r => setTimeout(r, TICKET_MS));
      const r = await api('/api/techdoc/result?ticket=' + encodeURIComponent(ticket));
      if (!r || !r.ok) return null;
      const d = await r.json().catch(() => null);
      if (d && d.status === 'done') return d;
    }
    return null;
  }

  async function loadStatus() {
    const r = await api('/api/techdoc/status');
    if (!r || !r.ok) return;
    const d = await r.json().catch(() => null);
    if (!d || !d.ticket) return;
    const out = await awaitTicket(d.ticket);
    if (out && !out.error) status = out;
  }

  // ── The card ─────────────────────────────────────────────────────────────────
  function render() {
    const mount = document.getElementById('techdocMount');
    if (!mount) return;

    const running = !!(prog && prog.running);
    const st = status && status.last_run_status;
    // "ok" beside a date says nothing; the status earns its space only when it is not ok.
    const abnormal = st && st !== 'ok';
    const when = status && status.last_run_at
      ? String(status.last_run_at).slice(0, 19).replace('T', ' ') : '—';

    mount.innerHTML = `
      <div class="info-card op-card td-card">
        <div class="info-card-title">Tech Documentation
          <span class="info-hint">docs.paloaltonetworks.com</span></div>

        <div class="info-row"><span class="info-label">Documents</span>
          <span class="info-value">${num(status && status.documents)}</span></div>
        <div class="info-row"><span class="info-label">Collected</span>
          <span class="info-value">${esc(when)}${
            abnormal ? ` <span class="td-state td-state-${esc(st)}">${esc(st)}</span>` : ''}</span></div>

        <div class="op-toolbar">
          <button class="op-btn" id="tdView" ${status && status.documents ? '' : 'disabled'}>
            ${IC.view}<span>View</span></button>
          <span class="op-sep"></span>
          <button class="op-btn op-btn-primary" id="tdUpdate">
            ${IC.update}<span>${running ? 'View progress' : 'Update'}</span></button>
        </div>
      </div>`;

    document.getElementById('tdView')?.addEventListener('click',
      // No extension: main.js derives the page id from the last path segment and looks it up in
      // PAGES, where every page is registered without one. The static cache appends ".html"
      // itself (StaticFileCache::normalize), so "/tech-doc" is the address and "/tech-doc.html"
      // is a file that happens to answer — and answers with no page shell around it.
      () => { location.href = '/tech-doc'; });
    // While a run is in flight this is the way back INTO it. It used to be disabled and read
    // "Updating…", so an operator who closed the progress window had no route back to it short
    // of reloading the page - and the run they were watching was still going.
    document.getElementById('tdUpdate')?.addEventListener('click',
      running ? openProgress : openConfirm);
  }

  // ── The window ───────────────────────────────────────────────────────────────
  const modal = () => window.NMS.modal;

  function paint(title, bodyHtml, footHtml) {
    const ov = modal().open(title, bodyHtml, footHtml);
    ov.querySelectorAll('[data-act]').forEach(
      el => el.addEventListener('click', () => ACTIONS[el.dataset.act]?.()));

    // The shared modal's own X and backdrop call closeModal(), which removes a class and tells
    // nobody. This module therefore never learned the window had gone, left `step` at 'running',
    // and the next poll a second later put it straight back on screen - the window could not be
    // dismissed at all while a crawl ran. Routed through ACTIONS.close so every way out of the
    // window leaves the same state behind.
    //
    // Assigned rather than addEventListener, and that is load-bearing: paint() runs on every
    // poll, while #cmClose and the overlay are created once and reused by every page that opens
    // a modal. addEventListener would stack a new handler on them each second.
    // Guarded on `step` because the overlay is shared: leave this page with a modal-less commit
    // review open later and the handler would still be sitting on it, dismissing someone else's
    // dialog into this module's state. When this card has nothing open, `step` is null and this
    // is a no-op.
    const dismiss = () => { if (step) ACTIONS.close(); };
    ov.onclick = (e) => { if (e.target === ov) dismiss(); };
    const x = ov.querySelector('#cmClose');
    if (x) x.onclick = dismiss;
    return ov;
  }

  function renderWindow() {
    if (step === 'confirm') {
      const lastRun = status && status.last_run_at
        ? String(status.last_run_at).slice(0, 10) : '—';
      return paint('Update Tech Documentation', `
        <p class="dlg-lede">Re-fetches every page the sitemap lists and replaces the corpus.</p>
        <dl class="dlg-facts">
          <dt>Stored now</dt><dd>${num(status && status.documents)} documents</dd>
          <dt>Last updated</dt><dd>${esc(lastRun)}</dd>
          <dt>Takes</dt><dd>about an hour</dd>
        </dl>
        <div class="dlg-note"><b>Runs on the appliance.</b> You can close this window or log out
          while it works, and any operator can reopen the card to watch it. Cancelling part-way
          is safe — documents already written stay written.</div>`,
        `<button class="btn-sm" data-act="close">Cancel</button>
         <button class="btn-sm btn-primary" data-act="start">Start update</button>`);
    }

    if (step === 'running' || step === 'ended') {
      const p = prog || {};
      const surveying = p.stage === 'survey';
      const done = p.done || 0, total = p.total || 0;
      const pct = total ? Math.min(100, Math.round((done / total) * 100)) : 0;
      const ended = step === 'ended';
      // The page the daemon is on. The whole point of the line: a run is forty minutes and the
      // counters move in steps, so this is what tells an operator it is alive and where it is.
      // Trimmed to the path — the host is the same 21,644 times and only costs reading room.
      const here = (p.current_url || '').replace(/^https?:\/\/[^/]+\//, '');
      const hereLine = here && !ended
        ? `<div class="td-here" title="${esc(p.current_url)}">${esc(here)}</div>` : '';
      // Labelled rather than narrated. The prose version - "1,593 pages · 15 redirect onto
      // another · 68 gone" - made the reader work out what each number counted every time they
      // looked, and the first one carried no label at all. The count of URLs surveyed so far is
      // not repeated here: the line above it already says `${done} / ${total}`.
      // All four buckets, always, so the row's shape never changes mid-run and the four numbers
      // can be read as adding up to the count surveyed. Unreachable was hidden while zero at
      // first; that read as "there is no such bucket" rather than "it has not happened", and a
      // column appearing part-way through is missed by anyone not watching at that moment.
      const unreachable = p.survey_unknown || 0;
      // Four readings that sum to the URLs surveyed, and whose Ok is the fetch's own total.
      // The survey probes redirect destinations too, so every one of these numbers is an answer
      // about a URL rather than a step in an explanation.
      const surveyLine = (p.survey_ok || p.survey_redirect || p.survey_missing || unreachable)
        ? `<div class="td-stage">
             <span class="td-stage-name">Survey</span>
             <span class="td-stage-vals">
               <span class="td-ok">Ok <b>${num(p.survey_ok)}</b> pages</span>
               <span>Redirect <b>${num(p.survey_redirect)}</b> pages</span>
               <span>Not found <b>${num(p.survey_missing)}</b> pages</span>
               <span>Unreachable <b>${num(unreachable)}</b> pages</span>
             </span>
           </div>`
        : '';

      // The fetch row keeps its place during the survey so the two phases are visible as one
      // shape from the start, but reads "—" rather than 0: nothing has been written yet, and a
      // zero there reports a failure where the honest answer is "not this phase".
      const countsLine = `
          <div class="td-stage${surveying ? ' td-stage-idle' : ''}">
            <span class="td-stage-name">Fetch</span>
            <span class="td-stage-vals">
              <span>stored <b>${surveying ? '—' : num(p.stored)}</b></span>
              <span>skipped <b>${surveying ? '—' : num(p.rejected)}</b></span>
            </span>
          </div>`;
      return paint(ended ? 'Update finished' : 'Updating Tech Documentation', `
        <div class="td-progress">
          <div class="td-bar"><div class="td-bar-fill${ended ? '' : ' td-bar-live'}"
               style="width:${pct}%"></div></div>
          <div class="td-prog-meta">
            <span>${surveying ? `surveying ${num(done)} / ${num(total)} URLs` : `${num(done)} / ${num(total)}`}</span>
            <span class="info-hint">${esc(p.stage || '')}</span>
          </div>
          ${hereLine}
          ${surveyLine}${countsLine}
        </div>
        ${ended ? '' : `<div class="td-warning">${esc(WARNING)}</div>`}
        ${note ? `<div class="op-msg ${note.err ? 'err' : 'ok'}">${esc(note.text)}</div>` : ''}`,
        ended ? '<button class="btn-sm" data-act="close">Close</button>'
              : '<button class="btn-sm td-cancel" data-act="cancel">Cancel</button>');
    }
  }

  // Re-attach to a run already in flight. The poll is usually still alive (see close below),
  // so this only has to put the window back on screen.
  function openProgress() {
    note = null; step = 'running'; renderWindow();
    if (!polling) pollProgress();
  }

  const ACTIONS = {
    close() {
      modal().close();
      step = null;
      // The poll outlives the window while the crawl does. The card reads `prog.running` to
      // decide whether its button says Update or View progress, and a stopped poll freezes
      // that at whatever was true when the window closed - offering Update on an appliance
      // that is mid-run, which then answers 409.
      if (!(prog && prog.running)) stopPolling();
      render();
    },
    start() { doUpdate(); },
    cancel() { doCancel(); },
  };

  function openConfirm() { note = null; step = 'confirm'; renderWindow(); }

  async function doUpdate() {
    note = null;
    const r = await post('/api/techdoc/refresh', {});
    if (!r || !r.ok) {
      note = { text: r && r.status === 409 ? 'An update is already running on this appliance.'
                                           : 'Update could not be started.', err: true };
      step = 'ended'; prog = null; return renderWindow();
    }
    step = 'running'; prog = null; renderWindow();
    pollProgress();
  }

  async function doCancel() {
    const r = await post('/api/techdoc/cancel', {});
    note = (r && r.ok) ? { text: 'Cancelling — waiting for the crawl to stop.', err: false }
                       : { text: 'Cancel could not be delivered.', err: true };
    renderWindow();
  }

  // `polling` rather than a null check on `timer`: pollProgress clears the handle on entry and
  // only sets a new one after an await, so there is a window each second where a poll is in
  // flight and `timer` is null. Reading that as "not polling" starts a second chain, and two
  // chains double the request rate for the rest of the run.
  function stopPolling() { polling = false; if (timer) { clearTimeout(timer); timer = null; } }

  async function pollProgress() {
    stopPolling();
    polling = true;
    const r = await api('/api/techdoc/progress');
    const d = r && r.ok ? await r.json().catch(() => null) : null;

    if (d && !d.idle) {
      prog = d;
      if (d.final || !d.running) {
        const watching = step === 'running';
        await loadStatus();
        note = d.error ? { text: 'Update failed: ' + d.error, err: true }
             : d.stage === 'cancelled' ? { text: 'Cancelled. Documents already written were kept.', err: false }
             : { text: `Update complete — ${num(d.stored)} documents.`, err: false };
        // Only reopen the window for someone who still had it open. A run finishing is not a
        // reason to put a modal in front of an operator who closed it and went back to work;
        // the card behind it carries the new document count either way.
        step = watching ? 'ended' : null;
        render();
        if (watching) renderWindow();
        stopPolling();
        return;
      }
    }
    if (step === 'running') renderWindow();
    render();
    timer = setTimeout(pollProgress, POLL_MS);
  }

  // ── Mount ────────────────────────────────────────────────────────────────────
  // operation.js calls this on every render of the Operation page (reload, tab switch, Refresh,
  // any Save/Load/Import), so it must not open the window: doing so put the progress window back
  // in front of an operator who had closed it, every time the page redrew. Adopting a run only
  // means the card reads "View progress" and the poll keeps it true; the window is opened by that
  // button and nothing else.
  async function mount() {
    render();
    if (!status) { await loadStatus(); render(); }
    if (polling) return;
    const r = await api('/api/techdoc/progress');
    const d = r && r.ok ? await r.json().catch(() => null) : null;
    if (d && d.running && !polling) { prog = d.idle ? null : d; render(); pollProgress(); }
  }

  window.NMS.techdoc = { mount, stop: stopPolling };
})();
