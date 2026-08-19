/* techdoc.js — Configuration ▸ System Management ▸ Operation ▸ Tech Documentation.
 *
 * The assistant answers out of a corpus crawled from docs.paloaltonetworks.com, and Palo Alto
 * republishes those docs continuously, so this card is how an operator brings it current.
 *
 * The card itself stays a status line — how many documents, when they were last read. Everything
 * with detail in it opens a window:
 *
 *   View                 the corpus as a product/docset tree. The sitemap has no hierarchy of its
 *                        own (22,519 flat URLs), so this is the only structured view that exists.
 *   Check for Updates    what a refresh would do, listed by title and URL, before anything is
 *                        downloaded. A refresh is never a blind 21,769-page fetch.
 *   Update               the crawl, with the warning first and a progress bar after. Cancel stops
 *                        the crawl on the appliance, not just this page's view of it — pages
 *                        already written stay written and the next run skips them.
 *
 * Progress is polled rather than streamed: mgmtd keeps one live progress slot for the one refresh
 * it allows at a time, so a poll always reports the crawl's real state. That is also what lets the
 * window recover its progress bar after a reload, when the browser has forgotten it started a
 * refresh but the appliance has not.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;

  const POLL_MS = 1000;      // progress cadence; the crawl reports every 50 pages
  const TICKET_MS = 400;     // check/status ticket poll
  const TICKET_TRIES = 90;   // a check is one sitemap fetch; 36s is far past generous

  const WARNING = 'This operation is not performed asynchronously. Please do not close this window.';

  let status = null;    // CorpusStatus
  let check = null;     // CorpusCheck
  let prog = null;      // RefreshProgress + {running, idle}
  let busy = false;     // a check is in flight
  let timer = null;
  // 'check' | 'list' | 'confirm' | 'running' | 'ended' — the window's step, not the card's.
  let step = null;
  let note = null;      // { text, err } inside the window

  // protobuf's JSON mapping renders int64 as a *string* ("80639260"), not a number — so a plain
  // typeof check reports the corpus has no text at all. Accept either and coerce.
  const num = (n) => {
    if (n === null || n === undefined || n === '') return '—';
    const v = typeof n === 'number' ? n : Number(n);
    return Number.isFinite(v) ? v.toLocaleString() : '—';
  };

  const path = (u) => String(u || '').replace('https://docs.paloaltonetworks.com/', '');

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
    // "ok" beside a date says nothing; the status is only worth the space when it is not ok.
    const st = status && status.last_run_status;
    const abnormal = st && st !== 'ok';
    const when = status && status.last_run_at
      ? String(status.last_run_at).slice(0, 19).replace('T', ' ') : '—';

    mount.innerHTML = `
      <div class="info-card op-card td-card">
        <div class="info-card-title">Tech Documentation
          <span class="info-hint">docs.paloaltonetworks.com</span></div>

        <div class="info-row"><span class="info-label">Documents</span>
          <span class="info-value">${num(status && status.documents)}</span></div>
        <div class="info-row"><span class="info-label">Last crawl</span>
          <span class="info-value">${esc(when)}${
            abnormal ? ` <span class="td-state td-state-${esc(st)}">${esc(st)}</span>` : ''}</span></div>

        <div class="op-toolbar">
          <button class="op-btn" id="tdView" ${status ? '' : 'disabled'}><span>View</span></button>
          <span class="op-sep"></span>
          <button class="op-btn op-btn-load" id="tdCheck" ${busy || running ? 'disabled' : ''}>
            <span>${running ? 'Updating…' : busy ? 'Checking…' : 'Check for Updates'}</span></button>
        </div>
      </div>`;

    document.getElementById('tdView')?.addEventListener('click', openViewer);
    document.getElementById('tdCheck')?.addEventListener('click', openCheck);
  }

  // ── The window ───────────────────────────────────────────────────────────────
  const modal = () => window.NMS.modal;

  function paint(title, bodyHtml, footHtml) {
    const ov = modal().open(title, bodyHtml, footHtml);
    ov.querySelectorAll('[data-act]').forEach((el) => {
      el.addEventListener('click', () => ACTIONS[el.dataset.act]?.());
    });
    return ov;
  }

  function changeList() {
    const rows = (check.changes || []).map(c => `
      <div class="tdc-row">
        <span class="tdc-kind td-${esc(c.kind)}">${esc(c.kind)}</span>
        <div class="tdc-doc">
          <span class="tdc-title">${esc(c.title || '(untitled — never fetched)')}</span>
          <span class="tdc-url">${esc(path(c.url))}</span>
        </div>
        <span class="tdc-when">${c.lastmod ? esc(String(c.lastmod).slice(0, 10)) : '—'}</span>
      </div>`).join('');
    return rows || '<div class="cm-loading">Nothing to list.</div>';
  }

  function totalOf(c) {
    return (c.added || 0) + (c.changed || 0) + (c.removed || 0) + (c.retry || 0);
  }

  function summaryChips() {
    return `
      <div class="td-summary">
        <span class="td-chip td-added">${num(check.added)} added</span>
        <span class="td-chip td-changed">${num(check.changed)} changed</span>
        ${check.retry ? `<span class="td-chip td-retry">${num(check.retry)} retry</span>` : ''}
        <span class="td-chip td-removed">${num(check.removed)} withdrawn</span>
        <span class="info-hint">of ${num(check.total_in_scope)} pages in scope</span>
      </div>`;
  }

  function renderWindow() {
    if (step === 'check') {
      return paint('Check for Updates', '<div class="cm-loading">Reading the sitemap…</div>',
                   '<button class="btn-sm" data-act="close">Close</button>');
    }

    if (step === 'list') {
      const total = totalOf(check);
      if (!total) {
        return paint('Check for Updates',
          `${summaryChips()}<p class="td-clean">Everything in scope is current — nothing to fetch.</p>`,
          '<button class="btn-sm" data-act="close">Close</button>');
      }
      // "retry" is not "changed": those pages did not move, the last crawl could not read them.
      const only = check.retry === total
        ? '<p class="field-hint">All of these are pages the last crawl could not read, not pages that changed.</p>'
        : '';
      return paint('Check for Updates',
        `${summaryChips()}${only}<div class="tdc-list">${changeList()}</div>${
          check.truncated ? `<p class="field-hint">Showing the first ${num((check.changes||[]).length)}; the counts above are complete.</p>` : ''}`,
        `<button class="btn-sm" data-act="close">Close</button>
         <button class="btn-sm btn-primary" data-act="confirm">Update</button>`);
    }

    if (step === 'confirm') {
      return paint('Update Tech Documentation',
        `${summaryChips()}
         <div class="td-warning">${esc(WARNING)}</div>
         <p class="field-hint">Cancelling part-way is safe: pages already written stay written, and
           the next update skips them.</p>`,
        `<button class="btn-sm" data-act="list">Back</button>
         <button class="btn-sm btn-primary" data-act="start">Start update</button>`);
    }

    if (step === 'running' || step === 'ended') {
      const p = prog || {};
      const done = p.done || 0, total = p.total || 0;
      const pct = total ? Math.min(100, Math.round((done / total) * 100)) : 0;
      const ended = step === 'ended';
      const bar = `
        <div class="td-progress">
          <div class="td-bar"><div class="td-bar-fill${ended ? '' : ' td-bar-live'}" style="width:${pct}%"></div></div>
          <div class="td-prog-meta">
            <span>${num(done)} / ${num(total)}</span>
            <span class="info-hint">${esc(p.stage || '')}</span>
          </div>
          <div class="td-counts">
            <span>fetched <b>${num(p.fetched)}</b></span>
            <span>unchanged <b>${num((p.skipped_304 || 0) + (p.skipped_same_sha || 0))}</b></span>
            ${p.skipped_alias ? `<span>aliases <b>${num(p.skipped_alias)}</b></span>` : ''}
            <span>added <b>${num(p.added)}</b></span>
            <span>changed <b>${num(p.changed)}</b></span>
            ${p.failed ? `<span class="td-fail">failed <b>${num(p.failed)}</b></span>` : ''}
          </div>
        </div>`;
      return paint(ended ? 'Update finished' : 'Updating Tech Documentation',
        `${bar}${ended ? '' : `<div class="td-warning">${esc(WARNING)}</div>`}${
          note ? `<div class="op-msg ${note.err ? 'err' : 'ok'}">${esc(note.text)}</div>` : ''}`,
        ended ? '<button class="btn-sm" data-act="close">Close</button>'
              : '<button class="btn-sm td-cancel" data-act="cancel">Cancel</button>');
    }
  }

  // ── Actions ──────────────────────────────────────────────────────────────────
  const ACTIONS = {
    close() { stopPolling(); step = null; modal().close(); render(); },
    list()  { step = 'list'; renderWindow(); },
    confirm() { step = 'confirm'; renderWindow(); },
    start() { doUpdate(); },
    cancel() { doCancel(); },
  };

  async function openCheck() {
    busy = true; note = null; check = null; step = 'check';
    render(); renderWindow();

    const r = await post('/api/techdoc/check', {});
    const d = r && r.ok ? await r.json().catch(() => null) : null;
    const out = d && d.ticket ? await awaitTicket(d.ticket) : null;
    busy = false; render();

    if (!out || out.error) {
      step = 'ended'; prog = null;
      return paint('Check for Updates',
        `<div class="op-msg err">${esc(out ? ('Check failed: ' + out.error)
                                            : 'pretzel-ai did not answer in time.')}</div>`,
        '<button class="btn-sm" data-act="close">Close</button>');
    }
    check = out; step = 'list'; renderWindow();
  }

  async function doUpdate() {
    note = null;
    const r = await post('/api/techdoc/refresh', {});
    if (!r || !r.ok) {
      note = { text: r && r.status === 409 ? 'A refresh is already running on this appliance.'
                                           : 'Update could not be started.', err: true };
      step = 'ended'; prog = null; return renderWindow();
    }
    step = 'running'; prog = null; renderWindow();
    pollProgress();
  }

  async function doCancel() {
    const r = await post('/api/techdoc/cancel', {});
    note = (r && r.ok)
      ? { text: 'Cancelling — waiting for the crawl to stop.', err: false }
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
        // The counters only settle once the crawl has committed its last batch, so the card's
        // numbers are re-read rather than derived from the progress message.
        await loadStatus();
        note = d.error ? { text: 'Update failed: ' + d.error, err: true }
             : d.stage === 'cancelled' ? { text: 'Cancelled. Pages already written were kept.', err: false }
             : { text: 'Update complete.', err: false };
        check = null;
        render(); renderWindow();
        return;
      }
    }
    if (step === 'running') renderWindow();
    render();
    timer = setTimeout(pollProgress, POLL_MS);
  }

  // ── Viewer ───────────────────────────────────────────────────────────────────
  function viewerHtml() {
    const byProduct = new Map();
    (status.products || []).forEach((r) => {
      if (!byProduct.has(r.product)) byProduct.set(r.product, []);
      byProduct.get(r.product).push(r);
    });

    const totals = (rows) => rows.reduce((a, r) => ({
      documents: a.documents + (Number(r.documents) || 0),
      bodies: a.bodies + (Number(r.bodies) || 0),
      chars: a.chars + (Number(r.chars) || 0),
      failed: a.failed + (Number(r.failed) || 0),
    }), { documents: 0, bodies: 0, chars: 0, failed: 0 });

    const products = [...byProduct.entries()]
      .map(([name, rows]) => ({ name, rows, t: totals(rows) }))
      .sort((a, b) => b.t.documents - a.t.documents);
    const grand = totals(status.products || []);

    const body = products.map(({ name, rows, t }) => {
      const dedup = t.documents ? Math.round((1 - t.bodies / t.documents) * 100) : 0;
      const inner = rows.slice().sort((a, b) => Number(b.documents) - Number(a.documents))
        .map((r) => `
          <div class="tdv-row">
            <span class="tdv-doc">${esc(r.docset || '(root)')}</span>
            <span class="tdv-n">${num(r.documents)}</span>
            <span class="tdv-n tdv-dim">${num(r.bodies)}</span>
            <span class="tdv-n ${Number(r.failed) ? 'tdv-fail' : 'tdv-dim'}">${Number(r.failed) ? num(r.failed) : '·'}</span>
          </div>`).join('');
      return `
        <details class="tdv-prod">
          <summary>
            <span class="tdv-name">${esc(name)}</span>
            <span class="tdv-sum">${num(t.documents)} docs · ${dedup}% dedup${
              t.failed ? ` · <b class="tdv-fail">${num(t.failed)} unread</b>` : ''}</span>
          </summary>
          <div class="tdv-head"><span>docset</span><span>docs</span><span>bodies</span><span>unread</span></div>
          ${inner}
        </details>`;
    }).join('');

    return `
      <div class="tdv-top">
        <div><span class="tdv-k">Documents</span><span class="tdv-v">${num(grand.documents)}</span></div>
        <div><span class="tdv-k">Distinct bodies</span><span class="tdv-v">${num(grand.bodies)}</span></div>
        <div><span class="tdv-k">Products</span><span class="tdv-v">${num(products.length)}</span></div>
      </div>
      <p class="tdv-note">Grouped by the product and docset each URL implies — docs.paloaltonetworks.com
        publishes one flat sitemap with no hierarchy of its own. <b>Distinct bodies</b> counts unique
        page text: Palo Alto republishes the same page per product version, and whole URL subtrees
        redirect onto one document, so many URLs share one body.</p>
      <div class="tdv-tree">${body || '<div class="cm-loading">The corpus is empty.</div>'}</div>`;
  }

  async function openViewer() {
    if (!modal()) return;
    paint('Tech Documentation', '<div class="cm-loading">Loading…</div>',
          '<button class="btn-sm" data-act="close">Close</button>');
    if (!status || !(status.products || []).length) await loadStatus();
    if (!status) {
      return paint('Tech Documentation', '<div class="cm-loading">Could not read the corpus.</div>',
                   '<button class="btn-sm" data-act="close">Close</button>');
    }
    paint('Tech Documentation — ' + num(status.documents) + ' documents', viewerHtml(),
          '<button class="btn-sm" data-act="close">Close</button>');
  }

  // ── Mount ────────────────────────────────────────────────────────────────────
  // Called by operation.js after it renders the page, because that render replaces #contentBody
  // wholesale and would otherwise wipe this card.
  async function mount() {
    render();
    if (!status) { await loadStatus(); render(); }

    // A refresh started before this page was loaded is still running on the appliance; adopt it
    // rather than showing a resting card that lies about the state.
    const r = await api('/api/techdoc/progress');
    const d = r && r.ok ? await r.json().catch(() => null) : null;
    if (d && d.running) { prog = d.idle ? null : d; step = 'running'; render(); renderWindow(); pollProgress(); }
  }

  window.NMS.techdoc = { mount, stop: stopPolling };
})();
