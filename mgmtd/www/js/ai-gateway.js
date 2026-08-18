/* ai-gateway.js — Configuration ▸ AI Gateway.
 *
 * Two kinds of setting live on this page, and they are saved by two different mechanisms because
 * they are two different kinds of thing:
 *
 *   The credential      applies IMMEDIATELY (POST → ticket → poll). It is a secret, so it must not
 *                       enter running_config: that document is append-versioned, shown verbatim in
 *                       the Publish diff and written out by Save-to-file, and a key there would be
 *                       permanent and readable by every reviewer. It goes to inferd, which seals it
 *                       with credentials.key and hands the ciphertext to engined.
 *
 *   Everything else     is STAGED and goes out with Publish, like every other tab. Which model
 *                       answers, the system prompt, whether grounding is on — these are operator
 *                       declarations, and being able to diff and roll them back is the point.
 *
 * What is deliberately NOT editable here: the embedding model, the corpus table, and the gateway's
 * host/path. The first is the dangerous one — changing it puts the query in a different vector
 * space than the passages it is matched against, and nothing errors; retrieval just quietly gets
 * worse. The rule this page follows is: settings that fail loudly are exposed, settings that fail
 * silently are not.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;

  // Read live (not once): settings tabs switch client-side without a page load (see main.js).
  const activeTab = () => new URLSearchParams(location.search).get('tab') || 'sites';

  const DRAFT_KEY = 'ai-gateway';
  const GATEWAY_ID = 'portkey';

  // The staged half. Mirrors inferd.service.gateway in running_config — the commit entry below
  // merge_patches exactly these keys onto that path.
  const EMPTY = {
    default_model: '',
    system_prompt: '',
    max_tokens: 512,
    models: [],
    rag_enabled: true,
    bypass_gateway: false,
  };

  let deployed = JSON.parse(JSON.stringify(EMPTY));   // last committed, for the diff
  let state = JSON.parse(JSON.stringify(EMPTY));      // staged
  let credential = { configured: false, last_test_at: '', last_test_ok: null, last_test_note: '' };

  const clone = (o) => JSON.parse(JSON.stringify(o));
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

  // ── Staging provider ─────────────────────────────────────────────────────────
  // daemon+domain resolve to inferd.service.gateway on the engined side (CommitService), so the
  // values object is a partial patch of that block and nothing else on it is disturbed.
  const commitPayload = () => [{
    daemon: 'inferd',
    domain: 'gateway',
    values: {
      default_model: state.default_model,
      system_prompt: state.system_prompt,
      max_tokens: Number(state.max_tokens) || 512,
      models: state.models,
      rag_enabled: !!state.rag_enabled,
      bypass_gateway: !!state.bypass_gateway,
    },
  }];

  window.NMS.staging.register({
    key: DRAFT_KEY,
    dirty: () => !same(deployed, state),
    payload: commitPayload,
    before: () => ({ gateway: deployed }),
    after: () => ({ gateway: state }),
    onPublished() {
      deployed = clone(state);
      window.NMS.draft.clear(DRAFT_KEY);
    },
    // A default model that is not in the catalog would publish an assistant that cannot answer:
    // inferd rejects the id and every turn comes back BAD_REQUEST. Caught here rather than at the
    // first question, because the operator is holding the fix right now.
    problems() {
      const out = [];
      const ids = (state.models || []).map(m => m.id);
      if (!state.default_model) {
        out.push('AI Gateway: no default model is selected.');
      } else if (ids.indexOf(state.default_model) === -1) {
        out.push(`AI Gateway: the default model "${state.default_model}" is not in the catalog.`);
      }
      (state.models || []).forEach((m, i) => {
        if (!m.id) out.push(`AI Gateway: catalog row ${i + 1} has no model id.`);
      });
      return out;
    },
  });

  // ── Load ─────────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const cfg = await fetch('/api/settings/running-config', { credentials: 'same-origin' })
        .then(r => (r.ok ? r.json() : null));
      const gw = cfg && cfg.inferd && cfg.inferd.service && cfg.inferd.service.gateway;
      if (gw) {
        deployed = Object.assign(clone(EMPTY), {
          default_model: gw.default_model || '',
          system_prompt: gw.system_prompt || '',
          max_tokens: gw.max_tokens == null ? 512 : gw.max_tokens,
          models: Array.isArray(gw.models) ? clone(gw.models) : [],
          // Absent means on. A gateway block committed before these flags existed must not read as
          // "grounding disabled" — that would silently turn RAG off across an upgrade.
          rag_enabled: gw.rag_enabled !== false,
          bypass_gateway: gw.bypass_gateway === true,
        });
      }
    } catch (_) { /* leave the empty shape; the page still renders */ }

    // A draft outlives a tab switch and a page load, so it wins over the committed copy — the
    // operator's unpublished edits are the thing they expect to see when they come back.
    const draft = window.NMS.draft.get(DRAFT_KEY, null);
    state = draft ? Object.assign(clone(deployed), draft) : clone(deployed);

    await loadCredential();
  }

  async function loadCredential() {
    try {
      const s = await fetch(`/api/gateway/status?id=${encodeURIComponent(GATEWAY_ID)}`,
        { credentials: 'same-origin' }).then(r => (r.ok ? r.json() : null));
      if (s) credential = s;
    } catch (_) { /* keep the previous view */ }
  }

  const touch = () => {
    window.NMS.draft.set(DRAFT_KEY, state);
    window.NMS.staging.refresh();
  };

  // ── Render ───────────────────────────────────────────────────────────────────
  function credentialBadge() {
    if (!credential.configured) {
      return '<span class="cfg-badge warn">not configured</span>';
    }
    if (credential.last_test_ok === true) return '<span class="cfg-badge ok">configured</span>';
    if (credential.last_test_ok === false) return '<span class="cfg-badge err">configured · last test failed</span>';
    return '<span class="cfg-badge ok">configured</span>';
  }

  function modelRows() {
    if (!state.models.length) {
      return '<tr><td colspan="4" class="cfg-empty">No models. Add the ones your gateway account '
           + 'has integrations for.</td></tr>';
    }
    return state.models.map((m, i) => `
      <tr>
        <td><input class="cfg-in" data-mi="${i}" data-mk="id" value="${esc(m.id || '')}"
                   placeholder="gpt-4o"/></td>
        <td><input class="cfg-in" data-mi="${i}" data-mk="provider" value="${esc(m.provider || '')}"
                   placeholder="openai"/></td>
        <td><input class="cfg-in" data-mi="${i}" data-mk="label" value="${esc(m.label || '')}"
                   placeholder="GPT-4o"/></td>
        <td class="cfg-td-act"><button class="btn-ghost btn-sm" data-mdel="${i}">Remove</button></td>
      </tr>`).join('');
  }

  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'ai-gateway') return;

    const opts = state.models.map(m =>
      `<option value="${esc(m.id)}"${m.id === state.default_model ? ' selected' : ''}>` +
      `${esc(m.label || m.id)}</option>`).join('');

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">AI Gateway</span>
            <span class="cfg-h-sub">credential, model and guardrail routing for the Assistant</span>
          </div>
        </div>

        <div class="cfg-cards">
          <div class="cfg-card">
            <div class="cfg-card-h">Gateway credential ${credentialBadge()}</div>
            <p class="field-hint">Applied immediately — this one is not published with the rest.
              The key is encrypted before it is stored and is never shown again, here or anywhere
              else. Leave it blank to keep the current one.</p>
            <div class="field-row"><label>API key</label>
              <input type="password" id="gwKey" autocomplete="off"
                     placeholder="${credential.configured ? '•••••••• (stored)' : 'paste your gateway key'}"/></div>
            ${credential.last_test_at ? `<p class="field-hint">Last verified
              ${esc(credential.last_test_at)}${credential.last_test_note
                ? ' — ' + esc(credential.last_test_note) : ''}</p>` : ''}
            <div class="cfg-card-foot">
              <span class="cfg-card-msg" id="gwKeyMsg"></span>
              <button class="btn-primary btn-sm" id="gwKeySave">Save credential</button>
            </div>
          </div>

          <div class="cfg-card">
            <div class="cfg-card-h">Assistant</div>
            <div class="field-row"><label>Model</label>
              <select id="gwModel">${opts || '<option value="">— no models —</option>'}</select></div>
            <div class="field-row"><label>Max tokens</label>
              <input type="number" id="gwMaxTokens" min="1" max="32768"
                     value="${esc(state.max_tokens)}"/></div>
            <div class="field-row field-row-tall"><label>System prompt</label>
              <textarea id="gwPrompt" rows="6">${esc(state.system_prompt)}</textarea></div>
            <p class="field-hint">The model must be one your gateway account can reach. An unknown
              id is not silently substituted — the turn fails and says so.</p>
          </div>

          <div class="cfg-card cfg-card-wide">
            <div class="cfg-card-h">Model catalog</div>
            <p class="field-hint">Which models this appliance may ask for. pretzel cannot discover
              what your gateway account has integrated, so the list is yours to declare. The
              provider is the integration name the gateway routes by.</p>
            <table class="cfg-table">
              <thead><tr><th>Model id</th><th>Provider</th><th>Label</th><th></th></tr></thead>
              <tbody>${modelRows()}</tbody>
            </table>
            <div class="cfg-card-foot">
              <button class="btn-ghost btn-sm" id="gwAddModel">Add model</button>
            </div>
          </div>

          <div class="cfg-card">
            <div class="cfg-card-h">Routing</div>
            <div class="field-row field-row-check">
              <label><input type="checkbox" id="gwRag"${state.rag_enabled ? ' checked' : ''}/>
                Ground answers in the documentation corpus</label></div>
            <p class="field-hint">Off means the model answers from what it knows. Turns still work;
              they are just not grounded, and the Assistant says so.</p>

            <div class="field-row field-row-check">
              <label><input type="checkbox" id="gwBypass"${state.bypass_gateway ? ' checked' : ''}/>
                Bypass the AI gateway</label></div>
            <p class="field-hint danger-hint">Sends the turn straight to the model provider,
              <b>skipping the AIRS guardrail entirely</b> — nothing is inspected on the way out or
              back. Intended for demonstrating the difference, not for normal operation. The
              Assistant marks every bypassed answer as uninspected.</p>
          </div>
        </div>
      </div>`;

    wire();
  }

  // ── Wire ─────────────────────────────────────────────────────────────────────
  function wire() {
    const msg = (text, ok) => {
      const m = document.getElementById('gwKeyMsg');
      if (m) { m.textContent = text; m.className = 'cfg-card-msg ' + (ok ? 'ok' : 'err'); }
    };

    document.getElementById('gwModel')?.addEventListener('change', (e) => {
      state.default_model = e.target.value; touch();
    });
    document.getElementById('gwMaxTokens')?.addEventListener('input', (e) => {
      state.max_tokens = Number(e.target.value) || 512; touch();
    });
    document.getElementById('gwPrompt')?.addEventListener('input', (e) => {
      state.system_prompt = e.target.value; touch();
    });
    document.getElementById('gwRag')?.addEventListener('change', (e) => {
      state.rag_enabled = e.target.checked; touch();
    });
    document.getElementById('gwBypass')?.addEventListener('change', (e) => {
      state.bypass_gateway = e.target.checked; touch();
    });

    // Catalog edits repaint only on add/remove: repainting per keystroke would move focus out of
    // the field being typed in.
    document.querySelectorAll('[data-mi]').forEach(input => {
      input.addEventListener('input', (e) => {
        const i = Number(e.target.dataset.mi);
        const k = e.target.dataset.mk;
        if (state.models[i]) { state.models[i][k] = e.target.value; touch(); }
      });
    });
    document.querySelectorAll('[data-mdel]').forEach(btn => {
      btn.addEventListener('click', (e) => {
        state.models.splice(Number(e.target.dataset.mdel), 1); touch(); render();
      });
    });
    document.getElementById('gwAddModel')?.addEventListener('click', () => {
      state.models.push({ id: '', provider: '', label: '' }); touch(); render();
    });

    document.getElementById('gwKeySave')?.addEventListener('click', async () => {
      const key = (document.getElementById('gwKey').value || '').trim();
      if (!key) return msg('Enter a key, or leave the field alone to keep the current one.', false);

      msg('Saving…', true);
      try {
        const r = await fetch('/api/gateway/credential', {
          method: 'POST', credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ id: GATEWAY_ID, api_key: key }),
        });
        if (!r.ok) return msg('Save failed (HTTP ' + r.status + ').', false);

        const start = await r.json();
        const outcome = await pollCredential(start.ticket);

        // The field is cleared whatever happened: on success the key is stored and must not linger
        // in the DOM, and on failure re-typing it is safer than leaving a secret in a form the
        // operator may walk away from.
        document.getElementById('gwKey').value = '';

        if (!outcome) return msg('Saved, but the daemon did not confirm. Reload to check.', false);
        if (outcome.ok) {
          await loadCredential();
          render();
          return msg('Credential saved.', true);
        }
        msg(outcome.error || 'The daemon rejected the credential.', false);
      } catch (e) {
        msg('Request failed: ' + e.message, false);
      }
    });
  }

  // Same ticket/poll shape the connector tests use, and the same endpoint — inferd answers on the
  // seqNo mgmtd handed out, and mgmtd files it in the connector-test result store.
  async function pollCredential(ticket) {
    if (!ticket) return null;
    const wait = (ms) => new Promise(r => setTimeout(r, ms));
    for (let i = 0; i < 40; i++) {
      await wait(250);
      try {
        const r = await fetch(`/api/connector/test-result?ticket=${ticket}`, { credentials: 'same-origin' });
        if (!r.ok) continue;
        const body = await r.json();
        if (body && body.status !== 'pending') return body;
      } catch (_) { /* a dropped poll is not a failed save — the next one asks again */ }
    }
    return null;
  }

  // ── Init ─────────────────────────────────────────────────────────────────────
  const refreshTab = async () => { await load(); render(); };

  function activate() {
    render();
    window.NMS.onRefresh(refreshTab);
  }

  document.addEventListener('DOMContentLoaded', async () => {
    await load();
    window.NMS.staging.refresh();
    if (activeTab() === 'ai-gateway') activate();
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === 'ai-gateway') activate();
  });
})();
