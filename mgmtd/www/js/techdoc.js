/* Appliance-owned corpus refresh. Hiding the dialog never cancels the worker. */
(function () {
  'use strict';
  window.NMS = window.NMS || {};
  const { esc, icons: IC } = window.NMS.utils;
  const num = n => Number(n || 0).toLocaleString();
  let status = null, prog = null, statusError = '', notice = '';
  let timer = null, polling = false, generation = 0, mounting = false;
  let starting = false, cancelling = false;
  let exceptions = null, exceptionsOpen = false, exceptionsBusy = false, exceptionFilter = '';
  let exceptionTimer = null, exceptionSeq = 0;

  // A technical noun phrase per reason, and nothing else: "HTTP 404" rather than "Gone", because
  // the operator reading this row is about to open the page and check, and the wording they match
  // against is the protocol's. Anything unmapped falls back to its own name, so a reason added
  // later is visible before it is translated.
  const REASON = {
    unsupported_template:   'Unsupported template',
    http_404:               'HTTP 404',
    http_410:               'HTTP 410',
    excluded_url:           'Policy exclusion',
    excluded_destination:   'Invalid redirect target',
    navigation_only:        'Navigation only',
    content_root_missing:   'Incomplete response',
    title_missing:          'No title',
    image_only_topic:       'Image only',
    empty_topic:            'Empty topic',
    fetch_failed:           'Fetch failed',
    redirect_outside_scope: 'Out-of-scope redirect',
  };
  const reasonLabel = r => REASON[r] || r;

  // Three outcomes, because there are only three things that happen to a page. Two are verdicts
  // about the site; the third is this crawler admitting it could not read the page, which is the
  // only one anybody can act on - so it is the only one marked.
  const BUCKETS = [
    {key: 'collected', label: 'Collected'},
    {key: 'excluded',  label: 'Excluded'},
    {key: 'unknown',   label: 'Unknown', watch: true},
  ];
  const running = () => starting || !!prog?.running;
  const date = value => value ? new Date(value).toLocaleString() : '—';
  async function api(url, options) {
    const r = await fetch(url, Object.assign({credentials: 'same-origin', signal: AbortSignal.timeout(15000)}, options));
    if (r.status === 401) { location.href = '/'; throw Error('Session expired.'); }
    const d = await r.json();
    if (!r.ok) throw Error(d.error || `Request failed (${r.status}).`);
    return d;
  }
  // mgmtd answers these with a ticket and collects the real answer on a later poll, so every
  // read of pretzel-ai goes through the same two steps rather than each caller repeating them.
  async function ask(url) {
    const { ticket } = await api(url);
    for (let i = 0; i < 90; i++) {
      await new Promise(r => setTimeout(r, 400));
      const d = await api('/api/techdoc/result?ticket=' + encodeURIComponent(ticket));
      if (d.status !== 'done') continue;
      if (d.error) throw Error(d.error);
      return d;
    }
    throw Error('Request timed out.');
  }
  async function loadStatus() {
    try { status = await ask('/api/techdoc/status'); statusError = ''; }
    catch (e) { statusError = e.message; }
  }
  // Read from the run's stored observations, so this answers mid-run as well as afterwards.
  // A quiet call is the timer refreshing what is already on screen: it must not blank the list
  // to a spinner, and a blip must not replace the rows with an error — only an opening request
  // has nothing to fall back on and has to say so.
  async function loadExceptions(reason, quiet) {
    if (!quiet) { exceptionsBusy = true; exceptionFilter = reason || ''; updateProgress(); }
    const mine = ++exceptionSeq;
    try {
      const next = await ask('/api/techdoc/exceptions?limit=200&run=' + encodeURIComponent(prog?.run_id || 0)
        + '&reason=' + encodeURIComponent(exceptionFilter));
      if (mine === exceptionSeq) exceptions = next;
    } catch (e) {
      if (mine === exceptionSeq && !quiet) exceptions = {error: e.message, exceptions: [], tally: []};
    } finally {
      if (mine === exceptionSeq) { exceptionsBusy = false; updateProgress(); scheduleExceptionRefresh(); }
    }
  }
  // Five seconds, not the one the progress bar runs on: an answer costs a ticket round trip, and
  // rows that move under a reader once a second cannot be read at all. Only while the crawl is
  // still producing them, and only while the panel is open to show them.
  function scheduleExceptionRefresh() {
    clearTimeout(exceptionTimer);
    if (!exceptionsOpen || !running() || !document.getElementById('tdProgress')) return;
    exceptionTimer = setTimeout(() => loadExceptions(exceptionFilter, true), 5000);
  }
  function summary() {
    try { return JSON.parse(status?.last_run_summary || '{}'); } catch (_) { return {}; }
  }
  function render() {
    const el = document.getElementById('techdocMount');
    if (!el) return;
    const last = summary();
    el.innerHTML = `<div class="info-card op-card td-card">
      <div class="info-card-title">Tech Documentation <span class="info-hint">Palo Alto Networks</span></div>
      <div class="info-row"><span class="info-label">Documents</span><span class="info-value">${status ? num(status.documents) : '—'}</span></div>
      <div class="info-row"><span class="info-label">Last run</span><span class="info-value">${esc(date(status?.last_run_at))}</span></div>
      <div class="info-row"><span class="info-label">Status</span><span class="info-value">${running() ? 'Updating…' : esc((last.dry_run ? 'Preview · ' : '') + (status?.last_run_status || 'Not yet updated'))}</span></div>
      ${statusError || status?.last_run_error ? `<div class="op-msg err">${esc(statusError || status.last_run_error)}</div>` : ''}
      <div class="op-toolbar">
        <button class="op-btn" id="tdView">${IC.view}<span>Browse documents</span></button>
        <button class="op-btn op-btn-primary" id="tdUpdate" ${starting ? 'disabled' : ''}>${IC.update}<span>${running() ? 'View progress' : 'Update'}</span></button>
        ${prog?.final || status?.last_run_id ? '<button class="op-btn" id="tdLast">Last result</button>' : ''}
      </div></div>`;
    document.getElementById('tdView').onclick = () => { location.href = '/tech-doc'; };
    document.getElementById('tdUpdate').onclick = running() ? openProgress : openConfirm;
    document.getElementById('tdLast')?.addEventListener('click', () => {
      if (!running()) {
        const s = summary();
        prog = Object.assign({}, s, {final: true, running: false, stage: status?.last_run_status,
          error: status?.last_run_error, run_id: status?.last_run_id});
      }
      openProgress();
    });
  }
  function openConfirm() {
    const products = [...new Set((status?.products || []).map(p => p.product))].sort();
    const ov = window.NMS.modal.open('Update Tech Documentation', `<div id="tdConfirm">
      <p class="dlg-lede">Collect technical topics and refresh the documents available to the assistant.</p>
      <label for="tdScope">Product</label><select id="tdScope" class="tdp-search"><option value="">All products</option>${products.map(p => `<option value="${esc(p)}">${esc(p)}</option>`).join('')}</select>
      <label><input id="tdDryRun" type="checkbox"> Preview only — report decisions without changing the corpus</label>
      <p class="dlg-note">Pages that cannot be verified keep their existing copy. Confirmed removals are archived for recovery. Large unexpected changes defer cleanup.</p>
      <p class="td-warning">Runs on the appliance. You can close this window and return later.</p>
      <div id="tdStartError" role="alert"></div></div>`,
      '<button class="btn-sm" id="tdDismiss">Close</button><button class="btn-sm btn-primary" id="tdStart">Start</button>');
    ov.querySelector('#tdDismiss').onclick = () => window.NMS.modal.close();
    ov.querySelector('#tdStart').onclick = async () => {
      if (starting) return;
      const scope = ov.querySelector('#tdScope').value;
      const dry_run = ov.querySelector('#tdDryRun').checked;
      starting = true; ov.querySelector('#tdStart').disabled = true; render();
      try {
        await api('/api/techdoc/refresh', {method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({scope, dry_run})});
        prog = {running:true,stage:'discover',dry_run}; notice = ''; cancelling = false;
        // The operator may have closed or replaced the modal while POST was in flight.
        if (ov.classList.contains('open') && ov.querySelector('#tdConfirm')) openProgress();
        startPolling();
      } catch (e) {
        const out = ov.querySelector('#tdStartError'); if (out) out.textContent = e.message;
      } finally {
        starting = false;
        const button = ov.querySelector('#tdStart'); if (button) button.disabled = false;
        render();
      }
    };
  }
  function openProgress() {
    // The list belongs to whichever run this window is about, and opening the window is the only
    // moment that can change. Cleared here rather than on every poll, which would drop the list
    // out from under someone reading it.
    exceptions = null; exceptionsOpen = false; exceptionFilter = ''; clearTimeout(exceptionTimer);
    window.NMS.modal.open('Tech Documentation update', '<div id="tdProgress"></div>',
      '<button class="btn-sm" id="tdHide">Close</button><button class="btn-sm td-cancel" id="tdCancel">Cancel update</button>');
    document.getElementById('tdHide').onclick = () => { clearTimeout(exceptionTimer); window.NMS.modal.close(); };
    document.getElementById('tdCancel').onclick = async () => {
      if (cancelling) return;
      cancelling = true; updateProgress();
      try {
        await api('/api/techdoc/cancel', {method:'POST'});
        notice = 'Cancellation requested. Waiting for active requests to finish.';
      } catch (e) { cancelling = false; notice = e.message; }
      updateProgress();
    };
    updateProgress();
    if (running()) startPolling();
  }
  function updateProgress() {
    const box = document.getElementById('tdProgress');
    // Update in place: never reopen the shared modal on a timer.
    if (!box) return;
    const p = prog || {}, final = !!p.final;
    // The live stream sends `done`; a finished run is rebuilt from its stored summary, which
    // records the same number as `processed`. Reading only one left "Last result" at 0 / 22,192.
    const done = Number(p.done ?? p.processed ?? 0);
    const percent = p.total ? Math.min(100, Math.round(100 * done / p.total)) : 0;
    const label = {discover:'Discovering documents',fetch:'Reading and verifying',reconcile:'Reconciling corpus',done:'Completed',ok:'Completed',cancelled:'Cancelled',failed:'Failed'}[p.stage] || p.stage || 'Starting';
    // This rebuilds the whole box once a second, which silently returned the exception list to
    // the top on every tick — unreadable while scrolled into it. Read before the write.
    const scrolled = box.querySelector('.td-exc-rows')?.scrollTop || 0;
    box.innerHTML = `<p class="dlg-lede">${p.dry_run ? 'Preview · ' : ''}${esc(label)}</p>
      <div class="td-bar" role="progressbar" aria-label="Documents checked" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${percent}"><div class="td-bar-fill" style="width:${percent}%"></div></div>
      <div class="td-prog-meta"><span>${num(done)} / ${num(p.total)} URLs</span><span>${p.run_id ? 'Run #' + esc(String(p.run_id)) : ''}</span></div>
      <div class="td-here" title="${esc(p.current_url || '')}">${esc((p.current_url || '').replace('https://docs.paloaltonetworks.com/', ''))}</div>
      ${buckets(p)}
      ${exceptionsOpen ? exceptionList() : ''}
      ${p.error ? `<p class="op-msg err" role="alert">${esc(p.error)}</p>` : ''}
      ${notice ? `<p class="td-warning" role="status">${esc(notice)}</p>` : ''}
      ${!final ? '<p class="info-hint td-note">Closing this window keeps the update running.</p>' : ''}`;
    const rows = box.querySelector('.td-exc-rows');
    if (rows) rows.scrollTop = scrolled;
    wireProgress();
    const button = document.getElementById('tdCancel');
    if (button) { button.hidden = final; button.disabled = cancelling; button.textContent = cancelling ? 'Cancelling…' : 'Cancel update'; }
  }
  // Excluded merges the two verdicts about the site - not documentation, and gone from it -
  // because to an operator they are one fact: the page is correctly not in the corpus. The
  // reasons behind them stay one click away rather than on screen as four parallel numbers.
  function buckets(p) {
    const value = {collected: Number(p.collected || 0), excluded: Number(p.excluded || 0),
                   unknown: Number(p.unknown || 0)};
    const rows = BUCKETS.map(b => `
      <div class="td-bucket${b.watch && value[b.key] ? ' td-bucket-watch' : ''}">
        <span class="td-bucket-n">${num(value[b.key])}</span>
        <span class="td-bucket-name">${esc(b.label)}</span>
      </div>`).join('');
    // A disclosure rather than a button: what it reveals belongs to the counts above it, and a
    // button reads as an action taken elsewhere. Open, it joins the panel below into one card.
    const open = value.excluded + value.unknown;
    return `<div class="td-buckets">${rows}</div>
      ${open ? `<button class="td-disclose" id="tdExceptions" aria-expanded="${exceptionsOpen}">
        <span class="td-disclose-caret" aria-hidden="true"></span>${exceptionsOpen ? 'Hide' : 'Show'} details
      </button>` : ''}`;
  }
  function exceptionList() {
    if (exceptionsBusy && !exceptions) return '<p class="cm-loading">Loading…</p>';
    if (exceptions?.error) return `<p class="op-msg err">${esc(exceptions.error)}</p>`;
    const items = exceptions?.exceptions || [], tally = exceptions?.tally || [];
    // Grouped under the bucket each reason belongs to, because the reason alone does not say
    // whether a page was ruled out or merely unread — and those have opposite consequences.
    // Grouped under the bucket each reason belongs to, because the reason alone does not say
    // whether a page was ruled out or merely unread — and those have opposite consequences.
    // Reasons that did not occur are listed too, dimmed: "checked, did not happen" is a fact
    // worth showing, and a row that simply vanishes cannot say it.
    const chip = t => `<button class="btn-sm td-chip${exceptionFilter === t.reason ? ' is-on' : ''}${
        t.count ? '' : ' is-nil'}" data-reason="${esc(t.reason)}" ${t.count ? '' : 'disabled'}>${esc(reasonLabel(t.reason))} <b>${num(t.count)}</b></button>`;
    const group = (label, key) => {
      const rows = tally.filter(t => t.decision === key);
      if (!rows.length) return '';
      const hit = rows.filter(t => Number(t.count) > 0), nil = rows.filter(t => !Number(t.count));
      return `<div class="td-chip-row"><span class="td-chip-label${key === 'unknown' ? ' is-watch' : ''}">${
        esc(label)}</span><span class="td-chip-set">${hit.concat(nil).map(chip).join('')}</span></div>`;
    };
    const chips = `<div class="td-chip-row"><span class="td-chip-label"></span><span class="td-chip-set">
        <button class="btn-sm td-chip${exceptionFilter === '' ? ' is-on' : ''}" data-reason="">All <b>${
        num(tally.reduce((n, t) => n + Number(t.count || 0), 0))}</b></button></span></div>
      ${group('Excluded', 'excluded')}
      ${group('Unknown', 'unknown')}`;
    const rows = items.map(e => `
      <div class="td-exc">
        <a href="${esc(e.url)}" target="_blank" rel="noopener noreferrer">${esc(e.url.replace('https://docs.paloaltonetworks.com/', ''))}</a>
        ${exceptionFilter ? '' : `<span class="td-exc-tag${e.decision === 'unknown' ? ' is-watch' : ''}">${esc(reasonLabel(e.reason))}</span>`}
        ${e.final_url && e.final_url !== e.url ? `<div class="td-exc-to">→ <a href="${esc(e.final_url)}" target="_blank" rel="noopener noreferrer">${esc(e.final_url.replace('https://docs.paloaltonetworks.com/', ''))}</a></div>` : ''}
        ${e.detail ? `<div class="td-exc-to">${esc(e.detail)}</div>` : ''}
      </div>`).join('');
    const shown = items.length, total = Number(exceptions?.total || 0);
    return `<div class="td-exc-panel"><div class="td-chips">${chips}</div>
      <div class="td-exc-rows">${rows || '<p class="cm-loading">No pages under this reason.</p>'}</div>
      ${total > shown ? `<p class="info-hint">${num(shown)} of ${num(total)}</p>` : ''}</div>`;
  }
  function wireProgress() {
    const toggle = document.getElementById('tdExceptions');
    if (toggle) toggle.onclick = () => {
      exceptionsOpen = !exceptionsOpen;
      if (exceptionsOpen && !exceptions) return loadExceptions('');
      updateProgress();
      scheduleExceptionRefresh();   // starts it on open, and clears it on close
    };
    document.querySelectorAll('#tdProgress .td-chip').forEach(chip => {
      chip.onclick = () => loadExceptions(chip.dataset.reason);
    });
  }
  function stopPolling() {
    generation++; polling = false; clearTimeout(timer); timer = null;
  }
  function startPolling() {
    if (polling) return;
    polling = true;
    const mine = ++generation;
    async function poll() {
      try {
        const d = await api('/api/techdoc/progress');
        if (mine !== generation) return;
        // A daemon restart may lose its in-memory progress slot. Do not display false success.
        if (d.idle && !d.running) {
          prog = {stage:'failed',final:true,error:'Progress is no longer available. Check the last run status.'};
        } else prog = Object.assign({}, prog, d);
        notice = ''; updateProgress(); render();
        if (!d.running || d.final) {
          stopPolling(); cancelling = false;
          await loadStatus(); render(); updateProgress(); return;
        }
      } catch (e) {
        if (mine !== generation) return;
        notice = 'Progress connection interrupted; retrying. ' + e.message; updateProgress();
      }
      if (mine === generation) timer = setTimeout(poll, 1000);
    }
    poll();
  }
  async function mount() {
    render();
    if (mounting) return;
    mounting = true;
    try {
      await loadStatus();
      if (!polling) {
        const d = await api('/api/techdoc/progress');
        if (d.running) { prog = d; startPolling(); }
        else if (!d.idle) prog = d;
      }
    } catch (e) { statusError = e.message; }
    finally { mounting = false; render(); }
  }
  window.NMS.techdoc = {mount,stop:stopPolling};
})();
