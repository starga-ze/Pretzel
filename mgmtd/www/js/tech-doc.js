/* Paginated product / manual / version browser and literal URL/title search. */
(function () {
  'use strict';
  const { esc } = window.NMS.utils;
  const PAGE = 100, MIN_QUERY = 3;
  let status = null, statusError = '', statusLoading = false;
  let selected = null, query = '', items = [], total = 0, offset = 0;
  let loading = false, error = '', sequence = 0, timer = null;
  const expanded = new Set();
  const num = n => Number(n || 0).toLocaleString();
  const key = r => JSON.stringify([r.product, r.docset, r.version || '']);
  const date = x => x ? new Date(x).toLocaleDateString() : '—';
  async function api(url) {
    const r = await fetch(url, {credentials:'same-origin', signal:AbortSignal.timeout(15000)});
    if (r.status === 401) { location.href = '/'; throw Error('Session expired.'); }
    const data = await r.json();
    if (!r.ok || data.error) throw Error(data.error || `Request failed (${r.status}).`);
    return data;
  }
  async function request(url) {
    const {ticket} = await api(url);
    if (!ticket) throw Error('Invalid response from the appliance.');
    for (let i = 0; i < 90; i++) {
      await new Promise(r => setTimeout(r, 400));
      const d = await api('/api/techdoc/result?ticket=' + encodeURIComponent(ticket));
      if (d.status === 'done') return d;
    }
    throw Error('Request timed out. Please retry.');
  }
  async function refresh() {
    if (statusLoading) return;
    statusLoading = true; statusError = ''; render();
    try { status = await request('/api/techdoc/status'); }
    catch (e) { statusError = e.message; }
    finally { statusLoading = false; render(); }
  }
  function products() {
    const map = new Map();
    for (const row of status?.products || []) {
      if (!map.has(row.product)) map.set(row.product, []);
      map.get(row.product).push(row);
    }
    return [...map].sort(([a],[b]) => a.localeCompare(b));
  }
  function tree() {
    return products().map(([product, rows]) => `<details class="tdp-prod" data-product="${esc(product)}" ${expanded.has(product) ? 'open' : ''}>
      <summary><span class="tdp-name">${esc(product)}</span><span class="tdp-sum">${num(rows.reduce((n,r) => n + Number(r.documents),0))} documents</span></summary>
      <div class="tdp-books">${rows.sort((a,b) => a.docset.localeCompare(b.docset) || (b.version || '').localeCompare(a.version || '', undefined, {numeric:true})).map(r => `<button class="tdp-book${selected && key(r) === key(selected) ? ' active' : ''}" data-category="${esc(key(r))}" aria-pressed="${!!selected && key(r) === key(selected)}">
        <span>${esc(r.docset || 'General')} · ${esc(r.version || 'Unversioned')}</span><span class="tdp-book-n">${num(r.documents)}</span></button>`).join('')}</div></details>`).join('') || '<p class="cm-loading">No documents yet. Run an update from Operation.</p>';
  }
  function row(d) {
    // Only published documentation links can become clickable, even for legacy DB rows.
    let safe = false;
    try { const u = new URL(d.url); safe = u.protocol === 'https:' && u.hostname === 'docs.paloaltonetworks.com'; } catch (_) {}
    return `<div class="tdp-doc"><div class="tdp-doc-main">
      ${safe ? `<a class="tdp-doc-title" href="${esc(d.url)}" target="_blank" rel="noopener noreferrer">${esc(d.title)}</a>` : `<span class="tdp-doc-title">${esc(d.title)}</span>`}
      <span class="tdp-doc-url" title="${esc(d.url)}">${esc(d.url.replace('https://docs.paloaltonetworks.com/',''))}</span>
      <span class="tdp-doc-meta">${esc(d.version || 'Unversioned')}${d.information_type ? ' · ' + esc(d.information_type) : ''} · ${d.validated ? 'Verified technical topic' : 'Awaiting validation'} · Fetched ${esc(date(d.fetched_at))}</span></div>
      <span class="tdp-doc-n">${num(d.char_count)}</span><span class="tdp-doc-when">${esc(date(d.lastmod))}</span></div>`;
  }
  function panel() {
    const term = query.trim();
    if (term && term.length < MIN_QUERY) return '<p class="cm-loading">Enter at least 3 characters to search titles and URLs.</p>';
    if (!term && !selected) return '<p class="cm-loading">Choose a product, manual and version to view documents.</p>';
    const label = term ? `Search: ${term}` : [selected.product,selected.docset,selected.version || 'Unversioned'].filter(Boolean).join(' / ');
    return `<section class="tdp-docs" aria-busy="${loading}">
      <div class="tdp-doc-head"><span>${esc(label)}${loading ? '' : ' — ' + num(total) + ' documents'}</span><span>Characters · Published</span></div>
      ${loading ? '<p class="cm-loading" role="status">Loading documents…</p>' : error ? `<p class="cm-loading" role="alert">${esc(error)} <button class="btn-sm" id="tdpRetry">Retry</button></p>` : items.map(row).join('') || '<p class="cm-loading">No matching documents.</p>'}
      <div class="tdp-pagination"><button class="btn-sm" id="tdpPrev" ${loading || offset === 0 ? 'disabled' : ''}>Previous</button>
      <span>${total && !loading ? `${num(offset + 1)}–${num(offset + items.length)} of ${num(total)}` : ''}</span>
      <button class="btn-sm" id="tdpNext" ${loading || offset + items.length >= total ? 'disabled' : ''}>Next</button></div></section>`;
  }
  function render() {
    const el = document.getElementById('contentBody'); if (!el) return;
    const box = document.getElementById('tdpSearch');
    const caret = box && document.activeElement === box ? [box.selectionStart,box.selectionEnd] : null;
    el.innerHTML = `<div class="cfg-page"><div class="cfg-toolbar"><div class="cfg-toolbar-meta"><span class="cfg-h">PA Tech Docs</span><span class="cfg-h-sub">Technical documentation available to the assistant</span></div>
      <div class="cfg-toolbar-actions"><button class="btn-sm" id="tdpRefresh" ${statusLoading ? 'disabled' : ''}>${statusLoading ? 'Refreshing…' : 'Refresh'}</button><a class="btn-sm" href="/settings?tab=operation">Update corpus</a></div></div>
      ${statusError ? `<p class="op-msg err" role="alert">${esc(statusError)}</p>` : ''}
      <div class="tdp-stats"><div><span class="tdp-k">Documents</span><span class="tdp-v">${status ? num(status.documents) : '—'}</span></div>
        <div><span class="tdp-k">Distinct bodies</span><span class="tdp-v">${status ? num(status.bodies) : '—'}</span></div>
        <div><span class="tdp-k">Last run</span><span class="tdp-v">${esc(status?.last_run_status || '—')}</span></div></div>
      <label class="tdp-search-label" for="tdpSearch">Search document titles and URLs</label><input class="tdp-search" id="tdpSearch" type="search" maxlength="256" placeholder="Search all titles and URLs (3+ characters)…" value="${esc(query)}" autocomplete="off">
      ${!query.trim() ? `<div class="tdp-tree">${status ? tree() : statusLoading ? '<p class="cm-loading">Loading categories…</p>' : ''}</div>` : ''}
      ${panel()}</div>`;
    document.getElementById('tdpRefresh').onclick = async () => { await refresh(); if (selected || query.trim().length >= MIN_QUERY) loadPage(0); };
    document.getElementById('tdpSearch').oninput = e => {
      query = e.target.value; ++sequence; clearTimeout(timer); offset = 0; items = []; total = 0; error = '';
      loading = query.trim().length >= MIN_QUERY; render();
      if (loading) timer = setTimeout(() => loadPage(0), 300);
      else if (!query.trim() && selected) loadPage(0);
    };
    el.querySelectorAll('[data-category]').forEach(button => { button.onclick = () => {
      const [product,docset,version] = JSON.parse(button.dataset.category);
      selected = {product,docset,version}; expanded.add(product); loadPage(0);
    }; });
    el.querySelectorAll('.tdp-prod').forEach(d => d.addEventListener('toggle', () => {
      if (!d.isConnected) return;
      if (d.open) expanded.add(d.dataset.product); else expanded.delete(d.dataset.product);
    }));
    document.getElementById('tdpPrev')?.addEventListener('click', () => loadPage(Math.max(0,offset - PAGE)));
    document.getElementById('tdpNext')?.addEventListener('click', () => loadPage(offset + PAGE));
    document.getElementById('tdpRetry')?.addEventListener('click', () => loadPage(offset));
    if (caret) { const input = document.getElementById('tdpSearch'); input.focus(); input.setSelectionRange(...caret); }
  }
  async function loadPage(nextOffset) {
    const mine = ++sequence, term = query.trim();
    offset = nextOffset; loading = true; error = ''; render();
    const params = new URLSearchParams(Object.assign({}, selected || {}, {q:term,offset:String(nextOffset),limit:String(PAGE)}));
    try {
      const result = await request('/api/techdoc/documents?' + params);
      if (mine !== sequence) return;
      items = result.documents || []; total = Number(result.total) || 0;
      if (!items.length && nextOffset > 0) { loadPage(0); return; }
    } catch (e) { if (mine !== sequence) return; error = e.message; items = []; }
    if (mine === sequence) { loading = false; render(); }
  }
  document.addEventListener('DOMContentLoaded', refresh);
})();
