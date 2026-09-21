/* ai-model-card.js — Configuration ▸ System Management ▸ Operation ▸ AI Model.
 *
 * Refreshes what each AI vendor's account actually serves. The list used to be shipped inside
 * ai-provider.js, so a model released the week after a build could not be selected until the next
 * one; it is fetched from the vendor now and stored in ai_provider_model, and the provider editor
 * reads it from there.
 *
 * Update sends nothing but a vendor name. The key that authenticates the vendor's list endpoint is
 * the same one the turns use, it is already sealed in the appliance's store, and collectord opens
 * it there — so this card has no key field and a vendor with no key stored cannot be picked. Two
 * places to type one key is two places for them to disagree.
 *
 * It sits on Operation rather than beside the provider editor for the same reason Benchtest Data
 * does: it is an act, not a setting. Nothing here is staged, committed or versioned — the catalog
 * is what the vendors say, not something an operator authored.
 *
 * Mounted by operation.js rather than inlined: that module replaces #contentBody wholesale on
 * every render, which would wipe anything this card had drawn itself.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};
  const { esc } = window.NMS.utils;
  const modal = () => window.NMS.modal;

  // The vendors, in the order the provider editor lists them. Their labels only — which models each
  // one serves is the whole point of this card and is never hard-coded here.
  const VENDORS = [
    { id: 'openai',    label: 'OpenAI' },
    { id: 'google',    label: 'Google' },
    { id: 'anthropic', label: 'Anthropic' },
  ];

  let catalog = null;   // { <provider>: { fetched_at, models: [...] } } | null while unread
  let keyed = null;     // Set of provider ids with a key stored | null while unread
  let msg = null;       // { text, err }
  let busy = false;

  const IC = window.NMS.utils.icons;

  const trimTs = (s) => (s ? String(s).split('.')[0].replace('T', ' ') : '—');

  // ── Data ──────────────────────────────────────────────────────────────────

  async function load() {
    // Both reads, together: the card cannot say anything useful about a vendor without knowing
    // whether it has a key, and a vendor with no key is the one case where "0 models" is not a
    // problem to report.
    try {
      const [c, k] = await Promise.all([
        window.NMS.utils.fetchJSON('/api/ai/models'),
        window.NMS.utils.fetchJSON('/api/ai/credentials'),
      ]);
      if (!c || !k) return;                       // 401: fetchJSON already redirected

      catalog = (c && typeof c === 'object' && !c.error) ? c : {};
      if (c && c.error) msg = { text: c.error, err: true };

      // { providers: { <id>: { stored, updated_at?, last_test? } }, sealing_available }. The same
      // document the provider editor reads; only `stored` is needed here, and a key is never in it.
      keyed = new Set();
      const provs = (k && k.providers && typeof k.providers === 'object') ? k.providers : {};
      Object.keys(provs).forEach((id) => { if (provs[id] && provs[id].stored) keyed.add(id); });
    } catch (e) {
      catalog = {};
      keyed = new Set();
      msg = { text: 'could not read the model catalog', err: true };
    }
    render();
  }

  // ── Render ────────────────────────────────────────────────────────────────

  // One line per vendor: how many models it serves and when it last answered. A vendor with no key
  // says so instead of showing an empty count, because the two are fixed in different places.
  function vendorRow(v) {
    const entry = catalog && catalog[v.id];
    const hasKey = keyed ? keyed.has(v.id) : false;

    let value;
    if (catalog === null) value = '—';
    else if (!hasKey) value = '<span class="muted">no API key stored</span>';
    else if (!entry) value = '<span class="muted">not fetched yet</span>';
    else {
      const n = Array.isArray(entry.models) ? entry.models.length : 0;
      value = `${n} model${n === 1 ? '' : 's'} · ${esc(trimTs(entry.fetched_at))}`;
    }

    return `<div class="info-row"><span class="info-label">${esc(v.label)}</span>
              <span class="info-value">${value}</span></div>`;
  }

  function render() {
    const mount = document.getElementById('aiModelMount');
    if (!mount) return;

    const anyKeyed = keyed ? VENDORS.some(v => keyed.has(v.id)) : false;

    mount.innerHTML =
      `<div class="info-card op-card">
         <div class="info-card-title">AI Model
           <span class="info-hint">per vendor</span></div>

         ${VENDORS.map(vendorRow).join('')}

         <div class="op-toolbar">
           <button class="op-btn op-btn-primary" id="aiModelUpdate" ${anyKeyed && !busy ? '' : 'disabled'}
                   title="Fetch a vendor's model list with its stored API key">${IC.update}<span>Update</span></button>
         </div>

         ${!anyKeyed && keyed !== null
            ? '<p class="field-hint">No vendor has an API key stored. Add one in Configuration ▸ AI Provider first.</p>'
            : ''}
         ${msg ? `<div class="op-msg ${msg.err ? 'err' : 'ok'}">${esc(msg.text)}</div>` : ''}
       </div>`;

    wire();
  }

  function setMsg(text, err) { msg = { text, err: !!err }; render(); }

  // ── Update ────────────────────────────────────────────────────────────────
  //
  // Every vendor, always. Per-vendor buttons were the first shape and they were furniture: the
  // operator's question is "is my model list current", which is never about one vendor, and a
  // three-way choice made them answer it three times. What is worth seeing per vendor is how the
  // run WENT, so that is what the window shows.

  const modalStep = { at: null };   // null | 'confirm' | 'running' | 'ended'

  // One row per vendor for the run in flight: { state, detail }.
  //   waiting  not started yet        skipped  no API key stored
  //   running  the vendor is being asked
  //   ok       replaced, detail is the count
  //   failed   detail is why
  let runs = {};

  const STATE_LABEL = {
    waiting: 'waiting', running: 'asking…', ok: 'updated', failed: 'failed', skipped: 'skipped',
  };

  function paint(title, bodyHtml, footHtml) {
    const ov = modal().open(title, bodyHtml, footHtml);
    ov.querySelectorAll('[data-act]').forEach(
      el => el.addEventListener('click', () => ACTIONS[el.dataset.act]?.()));
    return ov;
  }

  function runRow(v) {
    const r = runs[v.id] || { state: 'waiting', detail: '' };
    return `<div class="info-row">
              <span class="info-label">${esc(v.label)}</span>
              <span class="info-value">
                ${r.detail ? `<span class="muted">${esc(r.detail)}</span>` : ''}
                <span class="aim-state aim-state-${r.state}">${STATE_LABEL[r.state] || r.state}</span>
              </span>
            </div>`;
  }

  function renderWindow() {
    if (modalStep.at === 'confirm') {
      const ready = VENDORS.filter(v => keyed && keyed.has(v.id));
      const without = VENDORS.filter(v => !(keyed && keyed.has(v.id)));
      return paint('Update AI models',
        `<p>Every vendor with an API key stored is asked for the models this appliance's account
            serves. Each one's list is replaced by what comes back; a vendor that cannot be reached
            keeps the list it has.</p>
         <p class="field-hint">Will be asked: ${ready.map(v => esc(v.label)).join(', ') || 'none'}.
            ${without.length
              ? `Skipped for want of an API key: ${without.map(v => esc(v.label)).join(', ')}.`
              : ''}</p>`,
        `<button class="btn-sm" data-act="close">Cancel</button>
         <button class="btn-sm btn-primary" data-act="start" ${ready.length ? '' : 'disabled'}>Update all</button>`);
    }

    if (modalStep.at === 'running' || modalStep.at === 'ended') {
      const ended = modalStep.at === 'ended';
      const okCount = VENDORS.filter(v => (runs[v.id] || {}).state === 'ok').length;
      return paint(ended ? 'Update finished' : 'Updating AI models',
        `${VENDORS.map(runRow).join('')}
         <p class="field-hint">${ended
            ? `${okCount} of ${VENDORS.length} vendors updated.`
            : 'One vendor at a time — each is an internet round trip.'}</p>`,
        ended
          ? `<button class="btn-sm btn-primary" data-act="close">Close</button>`
          // No Cancel: the request is already with collectord and the vendor, and a button that
          // closed the window without stopping either would be describing something it does not do.
          : `<button class="btn-sm" data-act="close" disabled>Working…</button>`);
    }
    return null;
  }

  const ACTIONS = {
    close() { modalStep.at = null; modal().close(); },
    start() { runAll(); },
  };

  function openConfirm() {
    runs = {};
    modalStep.at = 'confirm';
    renderWindow();
  }

  // Sequential rather than three at once. Each is a round trip through collectord to a vendor, the
  // window is reporting them one by one anyway, and three simultaneous fetches would have the card
  // reload its catalog three times at the end for one operator action.
  async function runAll() {
    modalStep.at = 'running';
    runs = {};
    VENDORS.forEach((v) => {
      runs[v.id] = (keyed && keyed.has(v.id))
        ? { state: 'waiting', detail: '' }
        : { state: 'skipped', detail: 'no API key stored' };
    });
    renderWindow();

    busy = true;
    render();

    for (const v of VENDORS) {
      if (runs[v.id].state !== 'waiting') continue;

      runs[v.id] = { state: 'running', detail: '' };
      renderWindow();

      const r = await fetchVendor(v.id);
      runs[v.id] = r;
      renderWindow();
    }

    modalStep.at = 'ended';
    renderWindow();

    busy = false;
    const okCount = VENDORS.filter(v => runs[v.id].state === 'ok').length;
    const failed = VENDORS.filter(v => runs[v.id].state === 'failed');
    setMsg(failed.length
      ? `${okCount} updated, ${failed.length} failed — see the window for which.`
      : `${okCount} vendor${okCount === 1 ? '' : 's'} updated.`, failed.length > 0);
    await load();
  }

  // → { state, detail } for one vendor. Never throws: the run is a loop over all of them, and one
  // vendor timing out must not take the other two down with it.
  async function fetchVendor(provider) {
    try {
      // Two hops on the far side — collectord asks engined for the sealed key, then calls the
      // vendor over the internet — so this is given a longer ceiling than a local read.
      const d = await window.NMS.utils.pollTicket(
        '/api/ai/models/update', '/api/ai/models/update-result',
        { dispatch: { method: 'POST',
                      headers: { 'Content-Type': 'application/json' },
                      body: JSON.stringify({ id: provider }) },
          everyMs: 500, tries: 80 });
      if (!d) return { state: 'failed', detail: 'signed out' };   // 401: already redirecting

      if (d.ok) {
        const n = Number(d.count) || 0;
        return { state: 'ok', detail: `${n} model${n === 1 ? '' : 's'}` };
      }
      return { state: 'failed', detail: d.message || d.error || 'the update failed' };
    } catch (e) {
      return { state: 'failed', detail: 'did not finish in time' };
    }
  }

  // ── Wiring ────────────────────────────────────────────────────────────────

  function wire() {
    document.getElementById('aiModelUpdate')?.addEventListener('click', openConfirm);
  }

  async function mount() {
    render();
    await load();
  }

  window.NMS.aiModelCard = { mount };
})();
