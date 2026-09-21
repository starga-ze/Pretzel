/* ai-provider.js — Configuration ▸ AI Provider.
 *
 * The vendors this appliance holds an account with, and which of their models it may ask for. An
 * ordinary Configuration tab: a table, an Add button, a slideover editor, edit and delete at the
 * right of each row, one Publish.
 *
 * An entry is the three things an operator actually chooses:
 *
 *   provider   which vendor. One of three, each addable once.
 *   API key    theirs, held sealed on the appliance.
 *   models     which of that vendor's models this appliance may ask for.
 *
 * Everything that used to be here and is not any more came out for the same reason — it was not a
 * choice an operator makes:
 *
 *   the endpoint   a fact about the vendor. Compiled into pretzel-ai, the side that has to speak
 *                  the vendor's dialect anyway. A console field for it only ever bought the chance
 *                  to point "openai" at something that is not OpenAI.
 *   enabled        a second way to say "off". Removing the row is the first, and it is how every
 *                  other list in this console works.
 *   the turn shape default model, system prompt, token cap, timeout. How pretzel-ai shapes a turn,
 *                  not a statement this appliance makes about somebody's vendor account.
 *
 * The id is the VENDOR — openai, google, anthropic — not the model family. It is the name the key
 * belongs to, the name the endpoint answers for, and the prefix on every model those two serve;
 * "claude/claude-opus-5" said the family twice and named the wrong owner.
 *
 * Two stores, one Publish. Where a change LANDS differs; when it takes effect does not:
 *
 *   running_config   which vendors are configured, and their model lists.
 *   sealed keys      each vendor's API key. It cannot go in running_config — that document is
 *                    append-versioned, rendered verbatim in the review diff and written out by
 *                    Save-to-file, and mgmtd refuses a commit carrying a key.
 *
 * A typed key is therefore staged browser-local (NMS.draft, this tab only, exactly as api-keys.js
 * stages a device password) and never enters the commit payload. Publish is still the only moment
 * anything happens: the staged config goes to the commit, and the staged key goes to
 * POST /api/ai/credential in onPublished, where it is sealed with AES-256-GCM and handed to engined.
 *
 * The model list is a fixed set of checkboxes per vendor. No search — the lists are short enough to
 * read — and no free-text row: a name an operator has to type exactly is a name they will mistype.
 *
 * Those checkboxes used to be built from a table in this file, which meant a model released after a
 * build could not be selected until the next one. They come from GET /api/ai/models now — what each
 * vendor last told the appliance it serves, fetched by collectord and stored in ai_provider_model.
 * A vendor whose catalog has never been fetched has nothing to pick, and the editor says so and
 * points at the card that fetches it rather than pretending the account is empty.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const TAB = 'ai-provider';
  const DRAFT_KEY = 'ai-provider';
  const SCOPE = 'pretzel-ai';

  const activeTab = () => new URLSearchParams(location.search).get('tab') || window.NMS.settingsDefaultTab;
  const { esc } = window.NMS.utils;

  // ── The vendors ──────────────────────────────────────────────────────────────
  // Closed, and closed downstream too: each id is an endpoint compiled into pretzel-ai, a sealed
  // key slot under the same name, and a row the commit schema will accept. A fourth would name a
  // vendor nothing can serve.
  //
  // What they SERVE is not here. It used to be, and it went stale between releases by construction
  // — vendors ship models on their own schedule and this file does not. These two fields are what
  // is genuinely this console's to know: how to write the vendor's name, and what family to call
  // it. `keyHint` went with the placeholder that showed it.
  const VENDORS = [
    { id: 'openai',    label: 'OpenAI',    family: 'GPT' },
    { id: 'google',    label: 'Google',    family: 'Gemini' },
    { id: 'anthropic', label: 'Anthropic', family: 'Claude' },
  ];

  // What each vendor last listed, from GET /api/ai/models:
  //   { <provider>: { fetched_at, models: [{ id, label, token_param? }] } }
  //
  // Null while unread, and an absent vendor means "never fetched" — which the picker renders as a
  // pointer at the AI Model card rather than as an empty account.
  let catalog = null;

  const vendorOf = (id) => VENDORS.find(v => v.id === id) || VENDORS[0];
  const catalogOf = (id) => {
    const e = catalog && catalog[id];
    return (e && Array.isArray(e.models)) ? e.models : [];
  };
  const knownModel = (pid, mid) => catalogOf(pid).find(m => m.id === mid) || null;

  // ── State ────────────────────────────────────────────────────────────────────
  const state = { list: [] };
  let deployed = [];
  // Per-vendor credential state from /api/ai/credentials — { stored, updated_at? }. Never a key:
  // the appliance cannot produce one and would not hand it back if it could.
  let creds = null;
  let sealingAvailable = true;

  let editIdx = null;         // index into state.list, or null when adding
  let draft = null;           // the editor's working copy, adopted on Save
  let keyDraft = '';          // the editor's key field, adopted into `pending` on Save
  let saveNote = '';
  let keyBanner = null;       // outcome of the last key store, shown after Publish

  const clone = (o) => JSON.parse(JSON.stringify(o));

  // Keys the operator has entered but not yet published. Browser-local and this tab only — the same
  // place and the same reasoning as api-keys.js's device passwords: a secret cannot ride a commit,
  // so it waits here until Publish sends it down its own path.
  //
  // A value is the key to store; null means "remove the stored key". Absent means untouched.
  const SECRET_KEY = 'ai-provider-keys';
  const pending = {
    all: () => window.NMS.draft.get(SECRET_KEY, {}),
    has: (id) => Object.prototype.hasOwnProperty.call(pending.all(), id),
    get: (id) => pending.all()[id],
    set(id, value) {
      const s = pending.all(); s[id] = value;
      window.NMS.draft.set(SECRET_KEY, s); window.NMS.staging.refresh();
    },
    drop(id) {
      const s = pending.all(); delete s[id];
      window.NMS.draft.set(SECRET_KEY, s); window.NMS.staging.refresh();
    },
    clear() { window.NMS.draft.set(SECRET_KEY, {}); },
    any: () => Object.keys(pending.all()).length > 0,
  };

  const keySealed = (id) => !!(creds && creds[id] && creds[id].stored);
  // What the appliance will hold after Publish — the staged change laid over what it holds now.
  const keyEffective = (id) => (pending.has(id) ? pending.get(id) !== null : keySealed(id));

  // pretzel-ai's own fallback when nothing names one (src/deployment/catalog.py). Spelled out here
  // because hoisting has to compare EFFECTIVE values: a model that says nothing is not a model with
  // no answer, it is a model that answers "max_tokens", and hoisting a different string above it
  // would silently change what that model sends.
  const DEFAULT_TOKEN_PARAM = 'max_tokens';

  const effectiveTokenParam = (id, m) => {
    const known = knownModel(id, String(m.id));
    return String((m && m.token_param) || (known && known.token_param) || DEFAULT_TOKEN_PARAM);
  };

  // The token parameter to hoist onto the vendor: whichever its models say most often.
  //
  // Majority rather than unanimity. Requiring every model to agree meant one exception among sixty
  // put the string back on all sixty-one — the arrangement that made running_config repeat itself
  // in the first place. With the majority hoisted, the exceptions are the only ones that write it,
  // which is what "provider default plus overrides" is supposed to mean.
  function dominantTokenParam(id, models) {
    const seen = new Map();
    for (const m of models) {
      const tp = effectiveTokenParam(id, m);
      seen.set(tp, (seen.get(tp) || 0) + 1);
    }
    let best = '', bestN = 0;
    for (const [tp, n] of seen) {
      if (n > bestN) { best = tp; bestN = n; }
    }
    return best;
  }

  function normalizeEntry(p) {
    const id = (p && p.id) || '';
    const raw = (Array.isArray(p && p.models) ? p.models : [])
      .filter(m => m && typeof m === 'object' && m.id);
    const shared = dominantTokenParam(id, raw);

    const out = { id };
    // Never written when it is only the fallback everything already resolves to: a vendor whose
    // models all take max_tokens says nothing, exactly as it did before any of this existed.
    if (shared && shared !== DEFAULT_TOKEN_PARAM) out.token_param = shared;

    out.models = raw.map(m => {
      const known = knownModel(id, String(m.id));
      const entry = { id: String(m.id), label: String(m.label || (known && known.label) || m.id) };
      // Only the exceptions, and they carry the EFFECTIVE value rather than what they happened to
      // be stored with — a model inheriting a hoisted string it does not want has to say so.
      const tp = effectiveTokenParam(id, m);
      if (tp !== (out.token_param || DEFAULT_TOKEN_PARAM)) entry.token_param = tp;
      return entry;
    });

    return out;
  }

  const normalize = (c) => {
    const src = (c && typeof c === 'object') ? c : {};
    const raw = Array.isArray(src.providers) ? src.providers
      : (src.providers && Array.isArray(src.providers.list) ? src.providers.list : []);
    return raw.filter(p => p && VENDORS.some(v => v.id === p.id)).map(normalizeEntry);
  };

  const takenIds = (exceptIdx) => state.list.filter((_, i) => i !== exceptIdx).map(p => p.id);
  const freeVendors = (exceptIdx) => VENDORS.filter(v => !takenIds(exceptIdx).includes(v.id));

  // ── Staging ──────────────────────────────────────────────────────────────────
  const stage = () => { window.NMS.draft.set(DRAFT_KEY, state.list); window.NMS.staging.refresh(); };

  async function load() {
    try {
      const d = await window.NMS.utils.loadSettings();
      if (!d) return;
      deployed = normalize((d.scopes || {})[SCOPE] || {});
    } catch (_) { deployed = []; }
    const staged = window.NMS.draft.get(DRAFT_KEY, null);
    state.list = staged ? staged.map(normalizeEntry) : clone(deployed);
    window.NMS.staging.refresh();
  }

  async function loadCreds() {
    try {
      const r = await fetch('/api/ai/credentials', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const d = await r.json();
      creds = d.providers || {};
      sealingAvailable = d.sealing_available !== false;
    } catch (_) { creds = null; }
  }

  // What the vendors last listed. Read on the same terms as the credential state above: a failure
  // leaves it empty rather than throwing, because the editor has something to say about a vendor
  // with no catalog and nothing to say about a page that did not render.
  async function loadCatalog() {
    try {
      const r = await fetch('/api/ai/models', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const d = await r.json();
      catalog = (d && typeof d === 'object' && !d.error) ? d : {};
    } catch (_) { catalog = {}; }
  }

  const commitPayload = () => [{ scope: SCOPE, domain: 'providers', values: { list: state.list } }];

  window.NMS.staging.register({
    key: DRAFT_KEY,
    // A key-only change is still a change. Without this the Publish button stays dark and the key
    // the operator entered sits in the browser until something else happens to be dirty.
    dirty: () => JSON.stringify(state.list) !== JSON.stringify(deployed) || pending.any(),
    payload: commitPayload,
    // The staged keys appear in the diff as words, never as values.
    //
    // Not cosmetic: commitFlow returns early when `before` and `after` serialise the same, so a
    // Publish that changed ONLY a key used to enable the button, open nothing, and store nothing.
    // The key state has to be part of the diff for the flow to run at all — and putting it there
    // is the honest thing anyway, because sealing a key is a change the review should show.
    //
    // "replaced" rather than "sealed" when one was already stored, so overwriting a key is a
    // visible difference and not two identical lines.
    before: () => ({ ai_providers: deployed, api_keys: keyStateView(false) }),
    after: () => ({ ai_providers: state.list, api_keys: keyStateView(true) }),
    onPublished() {
      deployed = clone(state.list);
      window.NMS.draft.clear(DRAFT_KEY);
      applyPendingKeys();
    },
    problems() {
      const out = [];
      state.list.forEach(p => {
        const v = vendorOf(p.id);
        if (!p.models.length)
          out.push(`AI Provider "${v.label}" has no models — it cannot serve a turn.`);
        // Judged against what Publish will leave behind, not against what is stored now: a key
        // entered in this same batch is about to be sealed.
        if (creds && !keyEffective(p.id))
          out.push(`AI Provider "${v.label}" has no API key — its turns will fail.`);
      });
      return out;
    },
  });

  // Every vendor's key as a word, before or after the staged changes are applied.
  const keyStateView = (staged) => {
    const out = {};
    VENDORS.forEach(v => {
      const sealed = keySealed(v.id);
      if (!staged || !pending.has(v.id)) { out[v.label] = sealed ? 'sealed' : 'not set'; return; }
      const value = pending.get(v.id);
      out[v.label] = value === null ? 'not set' : (sealed ? 'sealed (replaced)' : 'sealed');
    });
    return out;
  };

  // ── Key store ────────────────────────────────────────────────────────────────
  // Sent on Publish, never before. Drains `pending` once the commit has been accepted, one request
  // per vendor, then re-reads the appliance so what the page reports is the stored state rather
  // than this function's optimism.
  async function applyPendingKeys() {
    const staged = pending.all();
    const ids = Object.keys(staged);
    if (!ids.length) return;

    const failed = [];
    for (const id of ids) {
      const value = staged[id];
      const body = value === null ? { id, clear: true } : { id, api_key: value };
      try {
        const r = await fetch('/api/ai/credential', {
          method: 'POST', credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body),
        });
        if (!r.ok) {
          const d = await r.json().catch(() => ({}));
          throw new Error(d.error || ('HTTP ' + r.status));
        }
      } catch (e) {
        failed.push(`${vendorOf(id).label}: ${e.message || e}`);
      }
    }

    // Cleared whether or not every request landed. A staged key that failed is not worth retrying
    // silently on the next unrelated Publish — the operator is told, and re-enters it if they mean
    // to. Keeping it would make a later Publish do something nobody asked for.
    pending.clear();
    await loadCreds();
    keyBanner = failed.length
      ? { bad: true, text: `Could not store ${failed.length} key: ${failed.join('; ')}` }
      : null;
    window.NMS.staging.refresh();
    render();
  }

  // ── Table ────────────────────────────────────────────────────────────────────
  // A key is a yes/no with a date attached, and the date is the part nobody reads. So the state is
  // a dot and a word, the age is relative ("2h ago" — the useful question is "is this the one I set
  // just now?", not the wall clock), and the exact timestamp goes on the title where it costs
  // nothing. Printing "sealed 2026-08-31 14:49" put the two most-scanned characters of the column
  // next to sixteen that are almost never needed.
  const keyCell = (p) => {
    const dot = (cls, label) => `<span class="ai-key ${cls}"><i></i>${esc(label)}</span>`;

    if (pending.has(p.id)) {
      return pending.get(p.id) === null
        ? dot('is-drop', 'Removing on publish')
        : dot('is-staged', 'Staged for publish');
    }
    if (creds === null) return dot('is-unknown', 'Unknown');
    if (!keySealed(p.id)) return dot('is-none', 'Not set');

    const at = (creds[p.id] || {}).updated_at;
    // Just the state. The column answers "is there a key", and the date — which ran into the word
    // beside it — is on the title for anyone who wants it and in the editor for anyone who needs it.
    return `<span title="${esc(at ? 'Sealed ' + window.NMS.utils.fmtTs(at) : 'Sealed')}">${
      dot('is-set', 'Sealed')}</span>`;
  };

  const MODELS_INLINE_LIMIT = 2;

  // In the cell: the readable name only. In the card: the name and the id the vendor is actually
  // sent, which is the one place that distinction is worth the width.
  const modelRowHtml = (m, withId) => `<div class="mdl-row">`
    + `<span class="mdl-name">${esc(m.label || m.id)}</span>`
    + (withId ? `<span class="mdl-id mono-val">${esc(m.id)}</span>` : '') + `</div>`;

  function modelsCell(p) {
    if (!p.models.length) return '<span class="muted">none</span>';
    const shown = p.models.slice(0, MODELS_INLINE_LIMIT);
    const extra = p.models.length - shown.length;
    return `<div class="mdl">
        <div class="mdl-count"><b>${p.models.length}</b> model${p.models.length === 1 ? '' : 's'}</div>
        ${shown.map(m => modelRowHtml(m, false)).join('')}
        ${extra > 0 ? `<div class="mdl-more">+${extra} more</div>` : ''}
      </div>`;
  }

  // Body-mounted so the table's overflow cannot crop it, and shared by every row.
  // Hiding is deferred so the pointer can travel from the cell into the card without the card
  // disappearing on the way. Sixty-one models do not fit on screen, so the card scrolls — and a
  // scrollable thing that vanishes when reached for is a thing that cannot be scrolled.
  let popHideTimer = null;
  const cancelPopHide = () => { clearTimeout(popHideTimer); popHideTimer = null; };
  const schedulePopHide = () => { cancelPopHide(); popHideTimer = setTimeout(hideModelPop, 140); };

  function modelPopEl() {
    let el = document.getElementById('aiModelPop');
    if (!el) {
      el = document.createElement('div');
      el.id = 'aiModelPop';
      el.className = 'ep-pop';
      el.addEventListener('mouseenter', cancelPopHide);
      el.addEventListener('mouseleave', schedulePopHide);
      document.body.appendChild(el);
    }
    return el;
  }

  function showModelPop(cell) {
    const p = state.list[+cell.dataset.models];
    // Only when the cell cannot show everything; at or below the inline limit it already does.
    if (!p || p.models.length <= MODELS_INLINE_LIMIT) return;

    const pop = modelPopEl();
    pop.innerHTML = `<div class="mdl-count"><b>${p.models.length}</b> models</div>`
      + p.models.map(m => modelRowHtml(m, true)).join('');
    pop.classList.add('open');

    // Measured at its natural height first: the cap below is only worth applying when the list
    // genuinely does not fit, and a stale one from the last provider would cut a short list short.
    pop.style.maxHeight = 'none';

    const GAP = 6, EDGE = 8;
    const r = cell.getBoundingClientRect();
    const pw = pop.offsetWidth, ph = pop.offsetHeight;

    let left = r.left;
    if (left + pw > window.innerWidth - EDGE) left = window.innerWidth - EDGE - pw;

    // Below, then above, then whichever side has more room with the card capped to it. The third
    // case is what sixty-one models hit: neither side fits, so the card takes the taller side and
    // scrolls inside itself rather than running off the screen.
    const below = window.innerHeight - r.bottom - GAP - EDGE;
    const above = r.top - GAP - EDGE;
    let top;
    if (ph <= below) {
      top = r.bottom + GAP;
    } else if (ph <= above) {
      top = r.top - GAP - ph;
    } else if (below >= above) {
      pop.style.maxHeight = `${below}px`;
      top = r.bottom + GAP;
    } else {
      pop.style.maxHeight = `${above}px`;
      top = EDGE;
    }

    pop.style.left = `${Math.max(EDGE, left) + window.scrollX}px`;
    pop.style.top = `${top + window.scrollY}px`;
  }

  const hideModelPop = () => document.getElementById('aiModelPop')?.classList.remove('open');

  const table = window.NMS.table.create({
    id: 'cfg.aiProviders',
    tableClass: 'cfg-table-aiprov',
    searchPlaceholder: 'Search providers…',
    empty: `<div class="cfg-empty">No AI providers yet — click <b>Add Provider</b> to add one.
              A provider is a vendor you hold an account with, its API key, and the models that
              account may be asked for.</div>`,
    onRows: (tbody) => {
      tbody.querySelectorAll('[data-edit]').forEach(b =>
        b.addEventListener('click', () => openEditor(+b.dataset.edit)));
      tbody.querySelectorAll('[data-del]').forEach(b =>
        b.addEventListener('click', () => removeEntry(+b.dataset.del)));
      tbody.querySelectorAll('[data-models]').forEach(cell => {
        cell.addEventListener('mouseenter', () => { cancelPopHide(); showModelPop(cell); });
        cell.addEventListener('mouseleave', schedulePopHide);
      });
    },
    columns: [
      { key: 'provider', label: 'Provider', cls: 'col-name', filter: 'enum',
        text: (p) => vendorOf(p.id).label,
        searchText: (p) => `${vendorOf(p.id).label} ${p.id} ${vendorOf(p.id).family}`,
        cell: (p) => `<div class="cell-name">${esc(vendorOf(p.id).label)}<span
            class="ai-fam">${esc(vendorOf(p.id).family)}</span></div>` },
      { key: 'key', label: 'API Key', cls: 'col-key', filter: 'enum',
        text: (p) => {
          if (pending.has(p.id)) return pending.get(p.id) === null ? 'Removing' : 'Staged';
          if (creds === null) return 'Unknown';
          return keySealed(p.id) ? 'Sealed' : 'Not set';
        },
        cell: keyCell },
      // 2 — the count inline, the names on hover. Every id is long and they are near-identical
      // within a vendor, so a row that printed them all would be a row nobody can scan. Same
      // anchor-on-the-cell shape the connector's Endpoint Control column uses.
      { key: 'models', label: 'Models', cls: 'col-models', filter: 'number',
        text: (p) => String(p.models.length),
        sortValue: (p) => p.models.length,
        searchText: (p) => p.models.map(m => `${m.id} ${m.label}`).join(' '),
        cell: (p, i) => `<div data-models="${i}">${modelsCell(p)}</div>` },
      { key: 'act', label: '', cls: 'col-act', sort: false,
        cell: (p, i) => `
          <button class="icon-btn" data-edit="${i}" title="Edit">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.12 2.12 0 0 1 3 3L12 15l-4 1 1-4z"/></svg>
          </button>
          <button class="icon-btn danger" data-del="${i}" title="Delete">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>
          </button>` },
    ],
  });

  const paintTable = () => {
    hideModelPop();
    const host = document.getElementById('aiTable');
    if (host) table.mount(host, state.list);
    const meta = document.getElementById('aiMeta');
    if (meta) {
      const n = state.list.length;
      const keys = Object.keys(pending.all()).length;
      meta.textContent = `${n} provider${n === 1 ? '' : 's'}`
        + (keys ? ` · ${keys} key change${keys > 1 ? 's' : ''} pending publish` : '');
    }
    // The button stays live even with every vendor configured. It used to disable itself, and a
    // dead button is a worse answer than an open panel: it says nothing about WHY, and the reason
    // — these three are already here — is exactly what the panel shows by greying them out. The
    // vendor list is short and fixed, so "all of them" is a state operators reach routinely.
  };

  async function removeEntry(idx) {
    const p = state.list[idx];
    if (!p) return;
    const v = vendorOf(p.id);
    const sealed = keySealed(p.id);
    const ok = await window.NMS.confirm({
      title: 'Remove provider',
      message: `Remove ${v.label}?`,
      detail: sealed ? 'Its stored API key is deleted on Publish as well.'
                     : 'Nothing happens until you publish.',
    });
    if (!ok) return;

    state.list.splice(idx, 1);
    // A key outlives its row only if nobody says otherwise, and a provider nobody can see is a key
    // nobody can manage. Removing the row stages the key's removal with it.
    if (sealed) pending.set(p.id, null); else pending.drop(p.id);
    stage();
    paintTable();
  }

  // ── Editor ───────────────────────────────────────────────────────────────────
  const isPicked = (mid) => draft.models.some(m => m.id === mid);

  // The picker's own view state, outside `draft` because none of it is part of the provider: it is
  // where the operator is looking, not what they have chosen. Reset when the editor opens.
  let modelQuery = '';

  // How many picks are named individually before the rest become a count. Deliberately small: at
  // eight the row wrapped to three lines and pushed the list it is supposed to help off screen,
  // which is the opposite of what it is for. Five fits one line at this width.
  const CHIP_LIMIT = 5;

  const matchesQuery = (m, q) =>
    !q || m.id.toLowerCase().indexOf(q) !== -1 || String(m.label || '').toLowerCase().indexOf(q) !== -1;

  // What is selected, named, above the list.
  //
  // This replaced a "Selected only" toggle, and the toggle was the weaker answer to the same
  // question: the header says HOW MANY are picked and the rows tint green, so the only thing left
  // to ask is WHICH — and that is not a thing to hide behind a button and then have to turn off
  // again. Named here, removable here, and visible while the operator searches for the next one.
  function pickChips() {
    if (!draft.models.length) return '';

    // Everything picked is a state the count already states, and sixty-one names do not add to it.
    // Naming them would be a wall of chips answering a question nobody has.
    const all = catalogOf(draft.id);
    if (all.length && draft.models.length === all.length) {
      return `<div class="ai-chips" id="aiPickChips">
          <span class="ai-chip-more">All ${all.length} selected.</span>
        </div>`;
    }

    const shown = draft.models.slice(0, CHIP_LIMIT);
    const rest = draft.models.length - shown.length;
    return `<div class="ai-chips" id="aiPickChips">
        ${shown.map(m => `<button type="button" class="ai-chip" data-unpick="${esc(m.id)}"
              title="Remove ${esc(m.id)}">${esc(m.label || m.id)}<span aria-hidden="true">&times;</span></button>`).join('')}
        ${rest > 0 ? `<span class="ai-chip-more">+${rest} more</span>` : ''}
      </div>`;
  }

  // Sixty-odd models per vendor, so a plain column of checkboxes is not a list an operator reads —
  // it is one they scroll past. Three things make it usable:
  //
  //   search      the model they want has a name they already know, and typing four characters of
  //               it beats scrolling a list ordered by a vendor's naming scheme
  //   chips       what is picked, named, without scrolling to find the ticks
  //   Select all  scoped to what the search is showing, so "every gpt-5.6" is one click. Clearing
  //               is NOT scoped — a Clear that left hidden selections behind would be a Clear that
  //               did not clear, and the count beside it would say so while looking wrong.
  //
  // An empty catalog is a state, not an impossibility: the list comes from the vendor, so a vendor
  // nobody has fetched yet has none. Said as the thing to do about it — the fetch lives on
  // Operation ▸ AI Model — rather than drawn as an account that serves nothing.
  function modelPicker() {
    const all = catalogOf(draft.id);
    if (!all.length) {
      return `<p class="field-hint">No models have been fetched for
                ${esc(vendorOf(draft.id).label)} yet. Store its API key here, publish, then run
                Update on the <b>AI Model</b> card in System Management ▸ Operation.</p>`;
    }

    const q = modelQuery.trim().toLowerCase();

    // Every row is rendered and the hidden ones are marked, not dropped. Filtering then costs one
    // attribute per row instead of rebuilding the list, which is what keeps the search box's focus
    // and caret where the operator left them.
    return `<div class="ai-pick-tools">
        <span class="ai-pick-search-wrap">
          ${window.NMS.utils.icons.search}
          <input type="search" class="ai-pick-search" id="aiPickSearch" autocomplete="off"
                 placeholder="Search ${esc(String(all.length))} models…" value="${esc(modelQuery)}">
        </span>
        <button type="button" class="ai-pick-act" data-pickall>Select all</button>
        <button type="button" class="ai-pick-act ai-pick-act-clear" data-pickclear>Clear</button>
      </div>
      ${pickChips()}
      <div class="ai-pick" id="aiPickList">
        ${all.map(m => `
          <label class="ai-pick-row${isPicked(m.id) ? ' on' : ''}" data-row="${esc(m.id)}"
                 ${matchesQuery(m, q) ? '' : 'hidden'}>
            <span class="tgl"><input type="checkbox" data-pick="${esc(m.id)}" ${isPicked(m.id) ? 'checked' : ''}/>
              <span class="tgl-track"></span></span>
            <span class="ai-pick-name">${esc(m.label)}</span>
            <span class="ai-pick-id mono-val">${esc(m.id)}</span>
          </label>`).join('')}
      </div>
      <p class="ai-pick-empty" id="aiPickEmpty" ${all.some(m => matchesQuery(m, q)) ? 'hidden' : ''}>Nothing matches.</p>`;
  }

  // Selection changes do NOT repaint the editor. Sixty rows rebuilt on every tick is visible work,
  // and it would take the search box's focus with it on the one interaction most likely to be
  // followed by more typing. The four things a pick can change are touched directly instead.
  function syncPick() {
    const body = document.getElementById('aiBody');
    if (!body || !draft) return;

    const all = catalogOf(draft.id);
    const q = modelQuery.trim().toLowerCase();
    let visible = 0;

    body.querySelectorAll('[data-row]').forEach((row) => {
      const m = all.find(x => x.id === row.dataset.row);
      if (!m) return;
      const on = isPicked(m.id);
      row.classList.toggle('on', on);
      const box = row.querySelector('[data-pick]');
      if (box) box.checked = on;

      const show = matchesQuery(m, q);
      row.hidden = !show;
      if (show) visible++;
    });

    const empty = document.getElementById('aiPickEmpty');
    if (empty) empty.hidden = visible > 0;

    const count = document.getElementById('aiPickCount');
    if (count) count.textContent = `${draft.models.length} of ${all.length} selected`;

    // The chip row is the one part that is rebuilt: its contents ARE the selection, so there is
    // nothing to toggle in place. It holds no focus worth preserving — the search box does, and it
    // is not inside this.
    const chips = body.querySelector('#aiPickChips');
    const tools = body.querySelector('.ai-pick-tools');
    const html = pickChips();
    if (chips) chips.outerHTML = html || '<div class="ai-chips" id="aiPickChips" hidden></div>';
    else if (html && tools) tools.insertAdjacentHTML('afterend', html);
    wireChips();
  }

  // Re-wired after every rebuild of the row above.
  function wireChips() {
    document.getElementById('aiPickChips')?.querySelectorAll('[data-unpick]').forEach(
      chip => chip.addEventListener('click', () => {
        draft.models = draft.models.filter(m => m.id !== chip.dataset.unpick);
        saveNote = '';
        syncPick();
      }));
  }

  // One model, as it is carried in the provider entry. The label and the token parameter come from
  // the catalog rather than being invented here: they are what the vendor said, and what pretzel-ai
  // needs to call the model.
  function pickEntry(mid) {
    const known = knownModel(draft.id, mid);
    const entry = { id: mid, label: (known && known.label) || mid };
    if (known && known.token_param) entry.token_param = known.token_param;
    return entry;
  }

  // Just the field. What a key is, where it goes and when it is applied is the page's business,
  // not something to re-explain in the one place an operator is trying to paste a value.
  function keyBlock(v) {
    if (!sealingAvailable) {
      return '<p class="field-hint bad">This appliance has no <code>/etc/pretzel/credentials.key</code>,'
           + ' so a key cannot be sealed.</p>';
    }

    const sealed = keySealed(v.id);
    const staged = pending.has(v.id) ? pending.get(v.id) : undefined;
    const mask = '••••••••••••••••';

    // Three states, told apart by the placeholder alone: nothing stored, something stored, and
    // something staged over it. The mask is a sentinel, never a value — it is not submitted, so
    // leaving the field untouched cannot overwrite a sealed key with dots.
    //
    // The fourth case — nothing stored — is now empty rather than the vendor's key format. What a
    // key looks like is a thing the operator is holding, not a thing the form has to tell them,
    // and it was the only one of the four that described instead of reported.
    const placeholder = staged === null ? 'removed on publish'
      : staged !== undefined ? `${mask}  staged`
      : sealed ? mask
      : '';

    return `<div class="ai-key-row">
        <input type="password" data-k autocomplete="off" spellcheck="false" class="mono-val"
               placeholder="${esc(placeholder)}" value="${esc(keyDraft)}" aria-label="API key"/>
        ${(sealed || staged !== undefined) ? `<button class="btn-sm danger" id="aiKeyClear" type="button">${
          staged === null ? 'Keep' : 'Remove'}</button>` : ''}
      </div>`;
  }

  function editorForm() {
    const v = vendorOf(draft.id);
    const free = freeVendors(editIdx);
    // The vendor is fixed for the life of an entry: it is the id the key is sealed under and the
    // prefix on every model in it, so changing it would not be an edit but a different provider.
    // Every vendor is listed, and the ones already configured are disabled in place rather than
    // dropped. A list that silently omitted them left an operator with three vendors set up
    // staring at an empty picker with nothing to tell them why; greyed-out rows that say
    // "configured" answer the question in the place it is asked.
    const taken = takenIds(editIdx);
    const picker = editIdx == null
      ? `<select data-f="id">
           ${!free.length ? '<option value="" selected>Every vendor is already configured</option>' : ''}
           ${VENDORS.map(x => {
             const isTaken = taken.includes(x.id);
             return `<option value="${esc(x.id)}"${isTaken ? ' disabled' : ''}${x.id === draft.id ? ' selected' : ''}>`
                  + `${esc(x.label)} · ${esc(x.family)}${isTaken ? ' — already configured' : ''}</option>`;
           }).join('')}
         </select>`
      : `<div class="ep-fixed">${esc(v.label)}<span class="lbl-sub">${esc(v.family)}</span></div>`;

    return `
      <div class="field-row"><label class="req">Provider</label>${picker}</div>
      <div class="field-row"><label class="req">API key</label>${keyBlock(v)}</div>

      <div class="ed-sec">
        <div class="ed-sec-h">Models
          <span class="info-hint" id="aiPickCount">${draft.models.length} of ${catalogOf(draft.id).length} selected</span></div>
        ${modelPicker()}
      </div>`;
  }

  function paintEditor() {
    const body = document.getElementById('aiBody');
    if (!body || !draft) return;
    body.innerHTML = editorForm();
    window.NMS.utils.clearInvalid(body);
    const note = document.getElementById('aiSaveNote');
    if (note) note.textContent = saveNote;
    window.NMS.utils.enhanceSelects(body);
    wireEditor();
  }

  function openEditor(idx) {
    editIdx = idx;
    if (idx == null) {
      // The first vendor not yet configured. With all of them taken there is nothing to select,
      // and the draft opens on none — the picker below renders every option disabled and Save
      // refuses, which reads as "there is nothing left to add" rather than as a broken button.
      const free = freeVendors(null);
      draft = { id: free.length ? free[0].id : '', models: [] };
    } else {
      draft = clone(state.list[idx]);
    }
    keyDraft = ''; saveNote = '';
    // The picker's view state belongs to the sitting, not to the provider: an editor opened again
    // should show the whole list, not the last search someone left in it.
    modelQuery = '';

    document.getElementById('aiTitle').textContent = idx == null ? 'Add Provider' : 'Edit Provider';
    document.getElementById('aiFoot').innerHTML = `
      <span class="ed-foot-note" id="aiSaveNote"></span>
      <button class="btn-sm" id="aiCancel" type="button">Cancel</button>
      <button class="btn-primary btn-sm" id="aiSave" type="button">Save</button>`;
    paintEditor();

    document.getElementById('aiCancel').onclick = closeEditor;
    document.getElementById('aiSave').onclick = saveEditor;

    document.getElementById('aiOverlay').classList.add('open');
    document.getElementById('aiPanel').classList.add('open');
  }

  const closeEditor = () => {
    editIdx = null; draft = null; keyDraft = ''; saveNote = '';
    document.getElementById('aiOverlay').classList.remove('open');
    document.getElementById('aiPanel').classList.remove('open');
  };

  function saveEditor() {
    // Says what is wrong at the foot of the panel and marks the field it is about — the note alone
    // does not say WHICH, and in a form with a picker, a key and a model list that is the question.
    const mark = (note, field) => {
      saveNote = note;
      paintEditor();
      window.NMS.utils.markInvalid(document.getElementById('aiBody'), field);
    };

    // Reachable only from the all-configured case above, where the picker has nothing selectable.
    if (!draft.id)
      return mark('Every vendor is already configured — edit one of them instead.', '[data-f="id"]');

    if (!draft.models.length)
      return mark('Select at least one model.', '.mdl-pick, [data-model]');
    // A new provider with no key would be committed and then fail every turn, and the operator is
    // standing in the one panel where they can fix it.
    if (editIdx == null && !keyDraft && !keySealed(draft.id))
      return mark('Enter the API key.', '[data-k]');

    // The key leaves the panel the same way the rest of it does — staged, not applied. It goes to
    // its own store rather than into the entry, so it cannot reach the commit payload even by
    // accident, but it is committed to by the same button.
    if (keyDraft) pending.set(draft.id, keyDraft);

    // Through normalizeEntry, which is what hoists a token parameter the whole vendor agrees on off
    // its models. The draft carries it per model because that is how the picker builds an entry
    // (pickEntry reads the catalogue), and storing the draft raw meant an edit put back the very
    // repetition a load had just removed — the entries that came from the server were slim and the
    // ones an operator touched were not.
    const entry = normalizeEntry(draft);
    if (editIdx == null) state.list.push(entry);
    else state.list[editIdx] = entry;

    stage();
    closeEditor();
    paintTable();
  }

  function wireEditor() {
    const body = document.getElementById('aiBody');

    // Add only, and it clears the selection with it: the catalogue belongs to the vendor, so
    // carrying the previous one across would offer models the new vendor does not serve.
    body.querySelector('select[data-f="id"]')?.addEventListener('change', (e) => {
      draft.id = e.target.value;
      draft.models = [];
      keyDraft = '';
      saveNote = '';
      // The catalogue changed underneath it, so a search over the old vendor's names means nothing.
      modelQuery = '';
      paintEditor();
    });

    body.querySelectorAll('[data-pick]').forEach(box => box.addEventListener('change', () => {
      const mid = box.dataset.pick;
      if (box.checked) {
        if (!draft.models.some(m => m.id === mid)) draft.models.push(pickEntry(mid));
      } else {
        draft.models = draft.models.filter(m => m.id !== mid);
      }
      saveNote = '';
      syncPick();
    }));

    // `input`, not `change`: the list narrows as it is typed, which is the whole point of having
    // it. No debounce — the filter is an attribute per row over a list of sixty.
    body.querySelector('#aiPickSearch')?.addEventListener('input', (e) => {
      modelQuery = e.target.value;
      syncPick();
    });

    // Scoped to what the search is showing. Selecting every model a vendor serves is rarely what
    // anyone means; selecting every one matching "gpt-5.6" often is.
    body.querySelector('[data-pickall]')?.addEventListener('click', () => {
      const q = modelQuery.trim().toLowerCase();
      catalogOf(draft.id).forEach((m) => {
        if (!matchesQuery(m, q)) return;
        if (!draft.models.some(x => x.id === m.id)) draft.models.push(pickEntry(m.id));
      });
      saveNote = '';
      syncPick();
    });

    // NOT scoped. A Clear that left the hidden selections behind would leave a count the operator
    // cannot account for from what is on screen.
    body.querySelector('[data-pickclear]')?.addEventListener('click', () => {
      draft.models = [];
      saveNote = '';
      syncPick();
    });

    wireChips();

    const keyInput = body.querySelector('[data-k]');
    // No repaint on input: a repaint per keystroke would take the caret with it.
    keyInput?.addEventListener('input', () => { keyDraft = keyInput.value; });

    // Removal is an intent, not an action: staged like everything else, taking effect on Publish.
    // Pressing it again withdraws the intent, which is why the label changes.
    document.getElementById('aiKeyClear')?.addEventListener('click', () => {
      if (pending.get(draft.id) === null) pending.drop(draft.id);
      else { pending.set(draft.id, null); keyDraft = ''; }
      paintEditor();
      paintTable();
    });
  }

  // ── Render ───────────────────────────────────────────────────────────────────
  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== TAB) return;

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">AI Provider</span>
            <span class="cfg-h-sub" id="aiMeta"></span>
          </div>
          <button class="btn-primary btn-sm" id="aiAdd">+ Add Provider</button>
        </div>

        ${keyBanner ? `<div class="ai-banner${keyBanner.bad ? ' bad' : ''}">${esc(keyBanner.text)}</div>` : ''}

        <div id="aiTable"></div>

      </div>

      <div class="slideover-overlay" id="aiOverlay"></div>
      <aside class="slideover" id="aiPanel">
        <div class="slideover-head">
          <span class="slideover-title" id="aiTitle">Provider</span>
          <button class="slideover-close" id="aiClose" type="button">&times;</button>
        </div>
        <div class="slideover-body" id="aiBody"></div>
        <div class="slideover-foot" id="aiFoot"></div>
      </aside>`;

    paintTable();

    document.getElementById('aiAdd').onclick = () => openEditor(null);
    document.getElementById('aiClose').onclick = closeEditor;
    document.getElementById('aiOverlay').onclick = closeEditor;
  }

  // ── Init ─────────────────────────────────────────────────────────────────────
  const refresh = async () => { await Promise.all([load(), loadCreds(), loadCatalog()]); render(); };

  function activate() {
    render();
    // Key state is only worth reading once the tab is actually open — every Configuration tab loads
    // this module. The table repaints when it lands, because the API Key column depends on it.
    if (creds === null) loadCreds().then(paintTable);
    // The catalogue is re-read on EVERY activation, not only when it has never been read. It is
    // filled from outside this page — Operation ▸ AI Model ▸ Update — so a `=== null` guard meant
    // an operator who fetched a vendor's models and came straight here was shown the empty answer
    // this tab had cached, with no way to correct it short of reloading the console.
    loadCatalog().then(() => { paintTable(); if (draft) paintEditor(); });
    window.NMS.onRefresh(refresh);
  }

  document.addEventListener('DOMContentLoaded', async () => {
    // Together: normalizeEntry falls back to the catalog for a label a stored entry does not carry,
    // so reading the config first would resolve those against an empty one.
    await Promise.all([loadCatalog(), load()]);
    if (activeTab() === TAB) activate();
    document.dispatchEvent(new Event('nms:ai-provider-ready'));
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === TAB) activate();
  });
})();
