/* tech-doc.js — the corpus browser, reached from the Tech Documentation card.
 *
 * What the assistant can answer out of, laid out so an operator can check it. The card upstairs
 * reports two numbers; this is where the documents themselves are.
 *
 * The tree is derived from URL paths rather than stored: docs.paloaltonetworks.com publishes one
 * flat sitemap with no hierarchy of its own, so product and book are read out of the path each
 * time. Every document links back to the page it was collected from — the corpus is a copy, and
 * the original is the thing to check it against.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};
  const { esc } = window.NMS.utils;

  const TICKET_MS = 400;
  const TICKET_TRIES = 90;
  const PAGE = 100;

  let status = null;
  let docs = null;          // loaded lazily, per product
  let openProduct = null;
  let filter = '';
  let shown = PAGE;

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

  // ── Rendering ────────────────────────────────────────────────────────────────
  function byProduct() {
    const map = new Map();
    (status.products || []).forEach((r) => {
      if (!map.has(r.product)) map.set(r.product, { rows: [], documents: 0, bodies: 0, chars: 0 });
      const p = map.get(r.product);
      p.rows.push(r);
      p.documents += Number(r.documents) || 0;
      p.bodies += Number(r.bodies) || 0;
      p.chars += Number(r.chars) || 0;
    });
    return [...map.entries()]
      .map(([name, v]) => ({ name, ...v }))
      .sort((a, b) => b.documents - a.documents);
  }

  function render() {
    const el = document.getElementById('contentBody');
    if (!el) return;

    if (!status) {
      el.innerHTML = `<div class="cfg-page"><div class="cm-loading">Loading the corpus…</div></div>`;
      return;
    }

    const products = byProduct();
    const q = filter.trim().toLowerCase();
    const visible = q ? products.filter(p => p.name.toLowerCase().includes(q)
                        || p.rows.some(r => (r.docset || '').toLowerCase().includes(q)))
                      : products;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">Tech Documentation</span>
            <span class="cfg-h-sub">collected from docs.paloaltonetworks.com</span>
          </div>
          <div class="cfg-toolbar-actions">
            <a class="btn-sm" href="settings?tab=operation">Back to Operation</a>
          </div>
        </div>

        <div class="tdp-stats">
          <div><span class="tdp-k">Documents</span><span class="tdp-v">${num(status.documents)}</span></div>
          <div><span class="tdp-k">Distinct bodies</span><span class="tdp-v">${num(status.bodies)}</span></div>
          <div><span class="tdp-k">Text</span><span class="tdp-v">${num(status.chars)} chars</span></div>
          <div><span class="tdp-k">Products</span><span class="tdp-v">${num(products.length)}</span></div>
          <div><span class="tdp-k">Collected</span><span class="tdp-v">${
            status.last_run_at ? esc(String(status.last_run_at).slice(0, 19).replace('T', ' ')) : '—'}</span></div>
        </div>

        <input class="tdp-search" id="tdpSearch" type="search" placeholder="Filter products and books…"
               value="${esc(filter)}" autocomplete="off">

        <div class="tdp-tree">${visible.map(productBlock).join('') ||
          '<div class="cm-loading">Nothing matches that filter.</div>'}</div>
      </div>`;

    wire();
  }

  function productBlock(p) {
    // documents vs distinct bodies is the share of text repeated across manuals and versions; it
    // varies enormously between products, which is why it is shown per product and not once.
    const dedup = p.documents ? Math.round((1 - p.bodies / p.documents) * 100) : 0;
    const open = openProduct === p.name;
    const books = p.rows.slice().sort((a, b) => Number(b.documents) - Number(a.documents));
    return `
      <details class="tdp-prod" ${open ? 'open' : ''} data-product="${esc(p.name)}">
        <summary>
          <span class="tdp-name">${esc(p.name)}</span>
          <span class="tdp-sum">${num(p.documents)} docs${dedup ? ` · ${dedup}% shared text` : ''}</span>
        </summary>
        <div class="tdp-books">
          ${books.map(b => `
            <button class="tdp-book" data-product="${esc(p.name)}" data-docset="${esc(b.docset || '')}">
              <span class="tdp-book-name">${esc(b.docset || '(top level)')}</span>
              <span class="tdp-book-n">${num(b.documents)}</span>
            </button>`).join('')}
        </div>
        ${open && docs ? docList() : ''}
      </details>`;
  }

  function docList() {
    const rows = docs.items.slice(0, shown).map(d => `
      <div class="tdp-doc">
        <div class="tdp-doc-main">
          <span class="tdp-doc-title">${esc(d.title)}</span>
          <a class="tdp-doc-url" href="${esc(d.url)}" target="_blank" rel="noopener noreferrer"
             title="Open on docs.paloaltonetworks.com">${esc(d.url.replace('https://docs.paloaltonetworks.com/', ''))}</a>
        </div>
        <span class="tdp-doc-n">${num(d.char_count)}</span>
        <span class="tdp-doc-when">${d.lastmod ? esc(String(d.lastmod).slice(0, 10)) : '—'}</span>
      </div>`).join('');
    const more = docs.items.length > shown;
    return `
      <div class="tdp-docs">
        <div class="tdp-doc-head">
          <span>${esc(docs.label)} — ${num(docs.items.length)} documents</span>
          <span class="tdp-doc-cols"><span>chars</span><span>updated</span></span>
        </div>
        ${rows || `<div class="cm-loading">${docs.error ? esc(docs.error) : 'Loading…'}</div>`}
        ${more ? `<button class="btn-sm tdp-more" id="tdpMore">Show ${num(Math.min(PAGE, docs.items.length - shown))} more</button>` : ''}
      </div>`;
  }

  function wire() {
    const search = document.getElementById('tdpSearch');
    if (search) {
      search.addEventListener('input', (e) => {
        filter = e.target.value;
        const at = e.target.selectionStart;
        render();
        const again = document.getElementById('tdpSearch');
        if (again) { again.focus(); again.setSelectionRange(at, at); }
      });
    }
    document.querySelectorAll('.tdp-book').forEach((b) => {
      b.addEventListener('click', (e) => {
        e.preventDefault();
        openBook(b.dataset.product, b.dataset.docset);
      });
    });
    document.querySelectorAll('.tdp-prod').forEach((d) => {
      d.addEventListener('toggle', () => {
        if (!d.open && openProduct === d.dataset.product) { openProduct = null; docs = null; }
      });
    });
    document.getElementById('tdpMore')?.addEventListener('click', () => { shown += PAGE; render(); });
  }

  async function openBook(product, docset) {
    openProduct = product; shown = PAGE;
    docs = { label: docset ? `${product} / ${docset}` : product, items: [], error: null };
    render();
    // The endpoint answers 202 with a ticket, not with the list: mgmtd hands the call to
    // pretzel-ai and returns immediately, and the answer is collected on a later poll. Reading
    // `documents` off the 202 finds nothing, which is exactly what an empty book looked like.
    const qs = new URLSearchParams({ product, docset });
    const r = await api('/api/techdoc/documents?' + qs.toString());
    const d = r && r.ok ? await r.json().catch(() => null) : null;
    const out = d && d.ticket ? await awaitTicket(d.ticket) : null;
    if (!out || out.error) {
      docs.error = out ? out.error : 'pretzel-ai did not answer in time.';
      docs.items = [];
    } else {
      docs.error = null;
      docs.items = out.documents || [];
    }
    render();
  }

  document.addEventListener('DOMContentLoaded', async () => {
    render();
    await loadStatus();
    render();
  });
})();
