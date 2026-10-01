/* user-requests.js — Site ▸ Control ▸ User Requests
 *
 * What this page is for
 * ---------------------
 * A Prisma Browser rule whose prompt mode is adminApproval does not block the user outright: it
 * asks them to explain themselves and files a request. This is the queue of those requests, read
 * through one site.
 *
 * Where the rows come from
 * ------------------------
 * GET /api/user-requests — rows out of pb_user_request, not a collection sample. engined projects
 * the collected list document into that table on every poll, so by the time this page asks there is
 * nothing to parse and nothing to pick: the queue survives a sample whose body has aged out, and a
 * request that has dropped off the vendor's newest page is still here.
 *
 * Each row carries one document: `list`, the row exactly as the list endpoint returned it. There
 * was a second — GET /user-requests/{id} per row per cycle — until it was measured against the same
 * request in both Pending and Approved states and found identical field for field. The panel shows
 * every field of the one that remains.
 *
 * The strip above the grid still comes from the collection overview, because freshness is a
 * property of the stream rather than of any row: this page is as current as the connector's
 * interval and should say so rather than let the operator assume "now".
 *
 * Acting on a request — approve, decline, revoke — is a POST to the vendor, so it does not travel
 * the collection path: mgmtd hands it to collectord on a ticket, the same shape every connector
 * test uses. Afterwards the page asks for a poll ahead of schedule rather than guessing the new
 * state, so what it shows is what the tenant said and not what the console hoped.
 */
(function () {
  'use strict';

  // fmtTs rather than toLocaleString: the shared helper renders the appliance's timestamps in the
  // viewer's local time without following the browser locale, which is the whole reason it exists.
  const { esc, fmtTs, relAge } = window.NMS.utils;
  const body = document.getElementById('contentBody');
  if (!body) return;

  // The endpoint subtype this page reads, as the collection overview reports it.
  const SUBTYPE = 'pab';

  const state = { site: '', loading: false, error: '', stream: null, rows: null, refreshing: false,
                  detail: { open: false, row: null, busy: false, result: null } };

  // The vendor's own enum, in its order. Once is first and is the default: it is the only value
  // that makes an approval single-use, which is the whole point of approving one of these.
  const BYPASS = ['Once', '10m', '1h', '4h', '9h', '12h', '24h', '3d', '7d', '14d', '30d', '60d', '90d'];

  const when = (iso) => (iso ? fmtTs.call(window.NMS.utils, iso) : '');
  const ago = (iso) => (iso ? relAge.call(window.NMS.utils, iso) : '');
  const slug = (v) => String(v || '').toLowerCase().replace(/[^a-z0-9]+/g, '-');
  const shortId = (v) => (String(v || '').length > 12 ? String(v).slice(0, 8) + '…' : String(v || ''));
  const field = (r, k) => (r && r.list ? r.list[k] : undefined);

  // adminBypassTimeframe comes back as a NUMBER OF MILLISECONDS, where the request side takes the
  // vendor's own labels ("Once", "10m", "24h"). Measured 2026-10-01: an approval made as 10m reads
  // back as 600000, and one made as Once reads back as 0 — milliseconds is the only reading that
  // lands every enum value on a round number.
  //
  // Rendered back into those labels rather than shown raw: "600000" in a column is a number the
  // operator has to divide before it means anything. Derived rather than table-looked-up so a value
  // the vendor adds later still reads as a duration instead of falling through to nothing.
  // Just the clock, not a full timestamp: this is only ever read against the attempt made seconds
  // ago, so the date would be noise in a line that has to stay one line.
  const nowClock = () => new Date().toTimeString().slice(0, 8);

  function bypassLabel(ms) {
    const n = Number(ms);
    if (!Number.isFinite(n)) return '';
    if (n === 0) return 'Once';
    const min = n / 60000;
    // Inclusive at the day boundary: the vendor's own label for 1440 minutes is "24h", not "1d",
    // and a value that reads back differently from the one that was chosen is a value the operator
    // has to translate twice.
    if (min < 60) return `${+min.toFixed(0)}m`;
    if (min <= 1440) return `${+(min / 60).toFixed(0)}h`;
    return `${+(min / 1440).toFixed(0)}d`;
  }

  async function api(url) {
    const r = await fetch(url, { credentials: 'same-origin', headers: { Accept: 'application/json' } });
    const d = await r.json().catch(() => null);
    if (!r.ok || (d && d.error)) throw Error((d && d.error) || `Request failed (${r.status}).`);
    return d;
  }

  // ── load ─────────────────────────────────────────────────────────────────────
  async function load() {
    state.site = window.NMS.utils.siteScope.get() || '';
    if (!state.site) { state.stream = null; state.rows = null; render(); return; }

    state.loading = true; state.error = ''; render();
    try {
      const site = encodeURIComponent(state.site);
      // Two reads, for two different subjects: the stream says how fresh any of this is, the rows
      // are the queue. Neither can answer the other's question.
      const [ov, q] = await Promise.all([
        api('/api/collection/overview?window=24&site=' + site),
        api('/api/user-requests?site=' + site),
      ]);

      const streams = (ov && Array.isArray(ov.streams) ? ov.streams : [])
        .filter(s => s.api_type === SUBTYPE)
        .sort((a, b) => String(b.last && b.last.at || '').localeCompare(String(a.last && a.last.at || '')));
      state.stream = streams[0] || null;
      state.rows = (q && Array.isArray(q.rows)) ? q.rows : [];

      if (state.detail.open && state.detail.row) {
        const again = state.rows.find(r => r.id === state.detail.row.id);
        if (again) state.detail.row = again;
      }
    } catch (e) {
      state.error = e.message;
    } finally {
      state.loading = false;
      publishBadge();
      render();
    }
  }

  // The sidebar count is Pending only. Approved, Declined and Revoked are history — a badge that
  // counted them would never fall back to nothing and would stop meaning "there is work here".
  function publishBadge() {
    const pending = (state.rows || []).filter(r => r.status === 'Pending').length;
    window.NMS.navBadge.set('navUserRequests', state.rows ? pending : 0);
  }

  // ── table ────────────────────────────────────────────────────────────────────
  const typeCell = (r) => `<span class="ur-badge ur-t-${esc(slug(r.type))}">${esc(r.type || '—')}</span>`;
  const statusCell = (r) => `<span class="ur-badge ur-s-${esc(slug(r.status))}">${esc(r.status || '—')}</span>`;

  const table = window.NMS.table.create({
    id: 'site.user-requests',
    tableClass: 'cfg-table-ur',
    searchPlaceholder: 'Search requests…',
    empty: () => `<div class="cfg-empty">No requests have been collected for this site yet.</div>`,
    onRows: (tbody) => {
      tbody.querySelectorAll('[data-ur-detail]').forEach((el) => {
        el.addEventListener('click', () => {
          const row = (state.rows || []).find(r => r.id === el.dataset.urDetail);
          if (row) openDetail(row);
        });
      });
    },
    // Newest first, declared rather than left to the server's ORDER BY alone: the rows already
    // arrive that way, but nothing on screen said so and the first thing anyone did was click a
    // header to find out. The operator's own sort, once they set one, is remembered and wins.
    defaultSort: { key: 'created', dir: 'desc' },
    columns: [
      // Read left to right as the request's own story: when it was asked, what kind, by whom,
      // against what, why — and only then what was decided. Status sits beside Responded because
      // they are one fact in two halves, and a verdict belongs after the thing it is a verdict on.
      { key: 'created', label: 'Requested', cls: 'col-ur-time', filter: false,
        text: (r) => r.created_at || '', sortValue: (r) => r.created_at || '',
        cell: (r) => r.created_at
          ? `<div class="cell-name">${esc(when(r.created_at))}</div>
             <div class="cell-sub">${esc(ago(r.created_at))}</div>`
          : '<span class="muted">—</span>' },
      { key: 'type', label: 'Type', cls: 'col-ur-type', filter: 'enum',
        text: (r) => r.type || '', cell: typeCell },
      { key: 'user', label: 'User', cls: 'col-ur-user', filter: 'text',
        // The list endpoint carries ids, not names — resolving them is a second endpoint and a
        // second collection. Shown short, with the whole id on hover so it can still be copied.
        text: (r) => field(r, 'userId') || '',
        cell: (r) => `<span class="ur-mono" title="${esc(field(r, 'userId') || '')}">${
          esc(shortId(field(r, 'userId')))}</span>` },
      { key: 'url', label: 'URL', cls: 'col-ur-url', filter: 'text',
        text: (r) => field(r, 'url') || '',
        cell: (r) => field(r, 'url')
          ? `<span class="ur-url" title="${esc(field(r, 'url'))}">${esc(field(r, 'url'))}</span>`
          : '<span class="muted">—</span>' },
      { key: 'reason', label: 'Reason', cls: 'col-ur-reason', filter: 'text',
        text: (r) => field(r, 'reason') || '',
        cell: (r) => field(r, 'reason')
          ? `<span class="ur-reason" title="${esc(field(r, 'reason'))}">${esc(field(r, 'reason'))}</span>`
          : '<span class="muted">—</span>' },
      { key: 'status', label: 'Status', cls: 'col-ur-status', filter: 'enum',
        text: (r) => r.status || '', cell: statusCell },
      { key: 'responded', label: 'Responded', cls: 'col-ur-time', filter: false,
        // Absent while Pending — the vendor omits the field rather than sending null, which is why
        // this reads it defensively instead of formatting undefined.
        //
        // An em-dash there said "nothing here" when what is true is "nobody has answered yet", and
        // those are the rows this page exists for. Said only when the status agrees: a row that is
        // Declined with no responseTime is something stranger than a wait, and should not be
        // dressed as one.
        text: (r) => r.response_time || (r.status === 'Pending' ? 'Awaiting review' : ''),
        sortValue: (r) => r.response_time || '',
        cell: (r) => {
          if (r.response_time) {
            const bypass = field(r, 'adminBypassTimeframe');
            return `<div class="cell-name">${esc(when(r.response_time))}</div>
              <div class="cell-sub">${esc(bypass != null
                ? 'bypass ' + bypassLabel(bypass) : ago(r.response_time))}</div>`;
          }
          return r.status === 'Pending'
            ? '<span class="ur-await">Awaiting review</span>'
            : '<span class="muted">—</span>';
        } },
      // Labelled rather than an icon: the three glyph buttons elsewhere in the console mean edit,
      // delete and test — all of which act. This one only opens, and a fourth glyph would be a
      // guess at which.
      { key: 'act', label: '', cls: 'col-act', sort: false, filter: false, search: false,
        cell: (r) => `<button class="btn-sm" data-ur-detail="${esc(r.id || '')}"
          title="Every field this request was collected with">View Details</button>` },
    ],
  });

  // ── detail + actions ─────────────────────────────────────────────────────────
  function openDetail(row) { state.detail = { open: true, row, busy: false, result: null }; renderDetail(); }
  function closeDetail() { state.detail.open = false; renderDetail(); }

  // Every field the vendor returned, in its own order rather than sorted: the order a tenant lists
  // them in is itself information, and alphabetising it hides which fields the vendor considers
  // primary. Nested values are shown as JSON rather than flattened — remoteApplication is one value
  // the vendor sends as one object.
  function fieldsHtml(row) {
    const doc = (row && row.list) || {};
    const keys = Object.keys(doc);
    if (!keys.length) return `<div class="cfg-empty">This request arrived with no fields.</div>`;

    return `<table class="cfg-table tbl cfg-table-ur ur-detail">
        <thead><tr><th>Field</th><th>Value</th></tr></thead>
        <tbody>${keys.map(k => {
          const v = doc[k];
          const text = (v === null || v === undefined) ? ''
            : (typeof v === 'object' ? JSON.stringify(v) : String(v));
          return `<tr><td>${esc(k)}</td><td>${
            text === '' ? '<span class="muted">—</span>' : `<span class="ur-mono">${esc(text)}</span>`
          }</td></tr>`;
        }).join('')}</tbody>
      </table>`;
  }

  // Revoke is offered whatever the status is, on purpose. The vendor documents it as withdrawing an
  // approval and says nothing about a Declined or still-Pending one; rather than guess, the button
  // is live and whatever the tenant answers is shown verbatim. Finding out costs one call.
  //
  // No hints and no example placeholders under these fields — see main.css on .field-row > label.req.
  // The result line below is state, not guidance: it says what the tenant just answered.
  function actionsHtml(row) {
    const d = state.detail;
    const pending = row.status === 'Pending';

    // Approve and decline are shown only while they apply. Disabling them instead left two dead
    // controls and a sentence explaining why they were dead, which is three things on screen to
    // say what their absence says by itself.
    const decide = pending ? `
          <div class="ur-act-sel"><select id="urBypass">
            ${BYPASS.map(v => `<option value="${esc(v)}">${esc(v)}</option>`).join('')}
          </select></div>
          <button class="btn-primary btn-sm" id="urApprove" ${d.busy ? 'disabled' : ''}>Approve</button>
          <button class="btn-sm" id="urDecline" ${d.busy ? 'disabled' : ''}>Decline</button>` : '';

    return `<div class="ur-act">
        <div class="field-row"><label for="urComment">Comment</label>
          <input id="urComment"/></div>
        <div class="ur-act-row">${decide}
          <button class="btn-sm danger" id="urRevoke" ${d.busy ? 'disabled' : ''}>Revoke</button>
        </div>
      </div>`;
  }

  async function act(action) {
    const d = state.detail;
    if (!d.row || d.busy) return;
    d.busy = true; d.result = null; renderDetail();

    const comment = (document.getElementById('urComment') || {}).value || '';
    const bypass = (document.getElementById('urBypass') || {}).value || '';

    try {
      const res = await window.NMS.utils.runConnectorTest('/api/user-requests/action',
        { id: d.row.id, action, comment, bypass: action === 'approve' ? bypass : '' },
        { everyMs: 700, tries: 40,
          startError: 'the appliance would not start that action',
          timeoutError: 'the tenant did not answer in time' });

      // Stamped with what was asked, what came back and when.
      //
      // Without the stamp a second attempt that answered exactly as the first did was
      // indistinguishable from the first one still being on screen — the row refreshes underneath
      // but the message does not, so "revoked it, then revoked it again" and "revoked it once"
      // rendered identically. The clock time is what separates two answers that read the same.
      const http = res && res.response && res.response.status;
      d.result = {
        ok: !!(res && res.ok),
        message: `${action} · ${http ? 'HTTP ' + http : 'no status'} · ${nowClock()}`
               + ` — ${(res && res.message) || 'no answer'}`,
      };
    } catch (e) {
      d.result = { ok: false, message: `${action} · ${nowClock()} — ${e.message}` };
    } finally {
      // Ask for a poll ahead of schedule rather than patching the row here: what the queue says
      // must be what the tenant says, and the only way to know that is to read it again.
      //
      // After a refusal as well as after a success, and that is the point. The refusals this API
      // gives are mostly staleness — "request is not approved" is what a request someone else has
      // already acted on answers — so the one moment the screen is most likely to be out of date is
      // the moment it used to refuse to re-read. The message stays; the row underneath catches up.
      await refreshNow();
      d.busy = false;
      renderDetail();
    }
  }

  // ── manual collection ────────────────────────────────────────────────────────
  // The connector's interval is a compromise: short enough to notice a new request, long enough not
  // to hammer the tenant. This is the escape hatch for the moment that compromise is wrong — after
  // acting on a request, or when someone says they have just submitted one.
  async function refreshNow() {
    if (!state.stream || state.refreshing) return;
    state.refreshing = true; render();
    try {
      await fetch('/api/collection/run-now', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ connector: state.stream.connector_oid, endpoint: state.stream.endpoint_oid }),
      });
      // The poll, the sample, the IPC hop and engined's write all have to land before the rows
      // change. Nothing reports back when they have, so this waits once rather than polling the
      // appliance in a loop for an answer that arrives in about a second.
      await new Promise(r => setTimeout(r, 1800));
    } catch (_) { /* the reload below still shows whatever is there */ }
    state.refreshing = false;
    await load();
  }

  function renderDetail() {
    const panel = document.getElementById('urPanel');
    const overlay = document.getElementById('urOverlay');
    if (!panel || !overlay) return;
    panel.classList.toggle('open', state.detail.open);
    overlay.classList.toggle('open', state.detail.open);
    if (!state.detail.open) return;

    const r = state.detail.row;
    document.getElementById('urDetailTitle').textContent =
      r ? `${r.type || 'Request'} · ${r.status || ''}` : 'Request';
    const d = state.detail;
    const result = d.result
      ? `<div class="${d.result.ok ? 'ur-ok' : 'ur-warn'}">${esc(d.result.message)}</div>`
      : '';
    document.getElementById('urDetailBody').innerHTML = r ? result + fieldsHtml(r) : '';

    const foot = document.getElementById('urDetailFoot');
    foot.innerHTML = r ? actionsHtml(r) : '';
    // The console's own select, not a bare one — every other editor enhances its selects and a
    // raw dropdown in this panel read as a different product.
    window.NMS.utils.enhanceSelects(foot);

    document.getElementById('urApprove')?.addEventListener('click', () => act('approve'));
    document.getElementById('urDecline')?.addEventListener('click', () => act('decline'));
    document.getElementById('urRevoke')?.addEventListener('click', () => act('revoke'));
  }

  // ── render ───────────────────────────────────────────────────────────────────
  // The house page header: a bold name and one muted line under it, exactly as every Configuration
  // tab writes its own.
  //
  // The name is the product's, not the endpoint's. It used to be whatever the operator had called
  // the endpoint — "sase-list-user-requests" — which is a detail of how the queue is fetched
  // showing where the queue itself should be named. The endpoint and connector are provenance, and
  // provenance belongs in the line underneath.
  //
  // That line leads with freshness because it is the only part an operator acts on: this page is a
  // snapshot taken on the connector's interval, and the question is always "is this current". The
  // exact instant is on hover rather than on screen — a timestamp wide enough to read precisely is
  // wide enough to bury the word that matters.
  function headerHtml() {
    const s = state.stream;
    const last = s && s.last;

    const sub = [
      last ? `updated ${ago(last.at)}` : 'never collected',
      s.interval_sec ? `polled every ${s.interval_sec}s` : '',
      s.connector_name || '',
    ].filter(Boolean).join(' · ');

    const exact = [
      s.endpoint_name ? `Endpoint: ${s.endpoint_name}` : '',
      last ? `Last collected ${when(last.at)}` : '',
    ].filter(Boolean).join('\n');

    return `<div class="cfg-toolbar">
        <div class="cfg-toolbar-meta">
          <span class="cfg-h">Prisma Browser User Requests</span>
          <span class="cfg-h-sub" title="${esc(exact)}">${esc(sub)}</span>
        </div>
        <button class="btn-sm" id="urCollectNow" ${state.refreshing ? 'disabled' : ''}
                title="Poll the tenant now instead of waiting for the connector's next interval">
          ${state.refreshing ? 'Collecting…' : 'Collect now'}</button>
      </div>`;
  }

  function warningsHtml() {
    const s = state.stream;
    const last = s && s.last;
    const warn = [];
    if (s.enabled === false) warn.push('This endpoint is disabled on its connector, so the queue stops here.');
    if (s.config_error) warn.push(esc(s.config_error));
    if (last && last.ok === false) warn.push(`The last collection failed${
      last.http_status ? ` (HTTP ${esc(String(last.http_status))})` : ''}. The rows below are from before it.`);
    return warn.map(w => `<div class="ur-warn">${w}</div>`).join('');
  }

  function render() {
    if (!state.site) {
      body.innerHTML = `<div class="cfg-empty">Choose a site in the sidebar to read its request queue.</div>`;
      return;
    }
    if (state.loading && !state.rows) {
      body.innerHTML = `<div class="cfg-empty">Reading this site's requests…</div>`;
      return;
    }
    if (state.error) {
      body.innerHTML = `<div class="cfg-empty">${esc(state.error)}</div>`;
      return;
    }
    if (!state.stream) {
      // Not an error: nothing is wrong with the appliance, the operator simply has not declared
      // this collection yet. Say what to declare and where, since both pages are two clicks away.
      body.innerHTML = `<div class="cfg-empty">
        <b>No Prisma Browser collection on this site.</b>
        <div>Define a <b>Prisma Browser</b> endpoint under
          <a href="settings?tab=api-endpoint">Configuration ▸ API Endpoint</a>, then add it to the
          site's SASE device in <a href="settings?tab=api-connector">Configuration ▸ API Connector</a>
          with the interval you want. Its requests appear here once the first collection lands.</div>
      </div>`;
      return;
    }

    body.innerHTML = `<div class="cfg-page">${headerHtml()}${warningsHtml()}
        <div id="urTable"></div>
      </div>

      <div class="slideover-overlay" id="urOverlay"></div>
      <aside class="slideover" id="urPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="urDetailTitle">Request</span>
          <button class="slideover-close" id="urDetailClose">&times;</button>
        </div>
        <div class="slideover-body" id="urDetailBody"></div>
        <div class="slideover-foot" id="urDetailFoot"></div>
      </aside>`;

    table.mount(document.getElementById('urTable'), state.rows || []);
    document.getElementById('urDetailClose').addEventListener('click', closeDetail);
    document.getElementById('urOverlay').addEventListener('click', closeDetail);
    document.getElementById('urCollectNow')?.addEventListener('click', refreshNow);
    renderDetail();
  }

  // ── boot ─────────────────────────────────────────────────────────────────────
  // onRefresh REGISTERS a handler — assigning to it replaces the registrar itself, which quietly
  // costs every page its in-place refresh and falls the topbar button back to a full reload.
  window.NMS.onRefresh(load);
  window.NMS.utils.siteScope.subscribe(load);
  document.addEventListener('DOMContentLoaded', load);
  if (document.readyState !== 'loading') load();
})();
