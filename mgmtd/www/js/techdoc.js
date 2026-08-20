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

  const POLL_MS = 1000;
  const TICKET_MS = 400;
  const TICKET_TRIES = 90;
  const WARNING = 'This operation is not performed asynchronously. Please do not close this window.';

  let status = null;   // CorpusStatus
  let prog = null;     // RefreshProgress + {running, idle}
  let timer = null;
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
            <span>View</span></button>
          <span class="op-sep"></span>
          <button class="op-btn op-btn-load" id="tdUpdate" ${running ? 'disabled' : ''}>
            <span>${running ? 'Updating…' : 'Update'}</span></button>
        </div>
      </div>`;

    document.getElementById('tdView')?.addEventListener('click',
      // No extension: main.js derives the page id from the last path segment and looks it up in
      // PAGES, where every page is registered without one. The static cache appends ".html"
      // itself (StaticFileCache::normalize), so "/tech-doc" is the address and "/tech-doc.html"
      // is a file that happens to answer — and answers with no page shell around it.
      () => { location.href = '/tech-doc'; });
    document.getElementById('tdUpdate')?.addEventListener('click', openConfirm);
  }

  // ── The window ───────────────────────────────────────────────────────────────
  const modal = () => window.NMS.modal;

  function paint(title, bodyHtml, footHtml) {
    const ov = modal().open(title, bodyHtml, footHtml);
    ov.querySelectorAll('[data-act]').forEach(
      el => el.addEventListener('click', () => ACTIONS[el.dataset.act]?.()));
    return ov;
  }

  function renderWindow() {
    if (step === 'confirm') {
      return paint('Update Tech Documentation', `
        <p>The sitemap is surveyed first — a HEAD for every URL, which resolves the ones that
           redirect and drops the ones that are gone — and then the pages that survive are
           fetched. The last run stored ${num(status && status.documents)} documents and took
           about an hour.</p>
        <div class="td-warning">${esc(WARNING)}</div>
        <p class="field-hint">Cancelling part-way is safe: documents already written stay written.</p>`,
        `<button class="btn-sm" data-act="close">Cancel</button>
         <button class="btn-sm btn-primary" data-act="start">Start update</button>`);
    }

    if (step === 'running' || step === 'ended') {
      const p = prog || {};
      const surveying = p.stage === 'survey';
      const done = p.done || 0, total = p.total || 0;
      const pct = total && !surveying ? Math.min(100, Math.round((done / total) * 100)) : 0;
      const ended = step === 'ended';
      // The survey has no per-item progress to report — it is one pass of HEADs — so the bar
      // sweeps rather than fills, and says what it is doing instead of pretending to a percentage.
      const surveyLine = (p.survey_ok || p.survey_redirect || p.survey_missing)
        ? `<div class="td-survey">${num(p.survey_ok)} pages · ${num(p.survey_redirect)} redirect
             onto another · ${num(p.survey_missing)} gone
             <span class="info-hint">of ${num(p.listed)} listed</span></div>`
        : '';
      return paint(ended ? 'Update finished' : 'Updating Tech Documentation', `
        <div class="td-progress">
          <div class="td-bar"><div class="td-bar-fill${ended ? '' : ' td-bar-live'}"
               style="width:${surveying ? 100 : pct}%${surveying ? ';opacity:.35' : ''}"></div></div>
          <div class="td-prog-meta">
            <span>${surveying ? `surveying ${num(total)} URLs…` : `${num(done)} / ${num(total)}`}</span>
            <span class="info-hint">${esc(p.stage || '')}</span>
          </div>
          ${surveyLine}
          <div class="td-counts">
            <span>stored <b>${num(p.stored)}</b></span>
            <span>skipped <b>${num(p.rejected)}</b></span>
          </div>
        </div>
        ${ended ? '' : `<div class="td-warning">${esc(WARNING)}</div>`}
        ${note ? `<div class="op-msg ${note.err ? 'err' : 'ok'}">${esc(note.text)}</div>` : ''}`,
        ended ? '<button class="btn-sm" data-act="close">Close</button>'
              : '<button class="btn-sm td-cancel" data-act="cancel">Cancel</button>');
    }
  }

  const ACTIONS = {
    close() { stopPolling(); step = null; modal().close(); render(); },
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

  function stopPolling() { if (timer) { clearTimeout(timer); timer = null; } }

  async function pollProgress() {
    stopPolling();
    const r = await api('/api/techdoc/progress');
    const d = r && r.ok ? await r.json().catch(() => null) : null;

    if (d && !d.idle) {
      prog = d;
      if (d.final || !d.running) {
        step = 'ended';
        await loadStatus();
        note = d.error ? { text: 'Update failed: ' + d.error, err: true }
             : d.stage === 'cancelled' ? { text: 'Cancelled. Documents already written were kept.', err: false }
             : { text: `Update complete — ${num(d.stored)} documents.`, err: false };
        render(); renderWindow();
        return;
      }
    }
    if (step === 'running') renderWindow();
    render();
    timer = setTimeout(pollProgress, POLL_MS);
  }

  // ── Mount ────────────────────────────────────────────────────────────────────
  async function mount() {
    render();
    if (!status) { await loadStatus(); render(); }
    // An update started before this page loaded is still running on the appliance; adopt it rather
    // than showing a resting card that lies about the state.
    const r = await api('/api/techdoc/progress');
    const d = r && r.ok ? await r.json().catch(() => null) : null;
    if (d && d.running) { prog = d.idle ? null : d; step = 'running'; render(); renderWindow(); pollProgress(); }
  }

  window.NMS.techdoc = { mount, stop: stopPolling };
})();
