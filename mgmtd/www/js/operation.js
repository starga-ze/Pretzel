/* operation.js — Configuration ▸ System Management ▸ Operation.
 *
 * Config lifecycle: what the running configuration is, how to take a copy of it, and how the
 * engine is doing applying the last commit. No staging provider — nothing here is edited into
 * the pending set.
 *
 * A word on vocabulary, because two different things want the word "revert":
 *   Discard  (topbar)  — throw away edits staged in this browser tab, keeping the committed one
 *   Rollback (here)    — re-commit an earlier version, changing what the daemons run
 * The topbar button is the staging control; this page is the version control.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // Read live (not once): settings tabs switch client-side without a page load (see main.js).
  const activeTab = () => new URLSearchParams(location.search).get('tab') || 'sites';

  const { esc } = window.NMS.utils;

  let reload = { status: 'idle', elapsed_ms: 0 };
  let queue = [];
  let running = null;   // { version, committed_at, config }

  async function load() {
    const get = (url) =>
      fetch(url, { credentials: 'same-origin', headers: { Accept: 'application/json' } })
        .then(r => (r.ok ? r.json() : null))
        .catch(() => null);

    const [r, q, rc] = await Promise.all([
      get('/api/settings/reload-status'),
      get('/api/settings/commit-queue'),
      get('/api/settings/running-config'),
    ]);
    if (r) reload = r;
    queue = Array.isArray(q) ? q : [];
    running = rc && rc.config ? rc : null;
  }

  const QUEUE_LABEL = { pending: 'Pending', running: 'Applying', done: 'Done', failed: 'Failed' };

  function render() {
    const el = document.getElementById('contentBody');
    if (!el || activeTab() !== 'operation') return;

    const rows = queue.length
      ? queue.map(t => `
        <tr>
          <td class="col-qid">#${esc(t.id)}</td>
          <td class="col-qst"><span class="q-${esc(t.status)}">${QUEUE_LABEL[t.status] || esc(t.status)}</span></td>
        </tr>`).join('')
      : `<tr><td colspan="2"><div class="cfg-empty">No commits yet in this engine session.</div></td></tr>`;

    const staged = window.NMS.staging && window.NMS.staging.anyDirty();

    el.innerHTML = `
      <div class="cfg-page">
        <div class="cfg-toolbar">
          <div class="cfg-toolbar-meta">
            <span class="cfg-h">Operation</span>
            <span class="cfg-h-sub">configuration lifecycle &amp; engine status</span>
          </div>
        </div>

        <div class="cfg-cards">
          <div class="cfg-card">
            <div class="cfg-card-h">Running configuration</div>
            <div class="cfg-card-big">${running ? 'Version ' + esc(running.version) : '—'}</div>
            <p class="field-hint">${running
              ? 'Committed ' + esc(running.committed_at || '') + '.'
              : 'Could not read the running configuration.'}
              ${staged ? '<b class="rc-dirty">Staged changes are not included.</b>' : ''}</p>
            <div class="cfg-card-foot">
              <button class="btn-sm" id="opView" ${running ? '' : 'disabled'}>View</button>
              <button class="btn-sm" id="opSave" ${running ? '' : 'disabled'}>Save to file</button>
            </div>
          </div>

          <div class="cfg-card">
            <div class="cfg-card-h">Reload</div>
            <div class="cfg-card-big">${reload.status === 'reloading' ? 'Applying…' :
                                       reload.status === 'complete' ? 'Converged' : 'Idle'}</div>
            <p class="field-hint">${reload.status === 'reloading'
              ? 'A committed configuration is being applied across the daemons ('
                + Math.round((reload.elapsed_ms || 0) / 1000) + 's elapsed).'
              : 'Commits are applied by engined; every service daemon reloads to the new version.'}</p>
          </div>

          <div class="cfg-card">
            <div class="cfg-card-h">Commit queue</div>
            <table class="cfg-table cfg-table-queue">
              <thead><tr><th class="col-qid">Task</th><th class="col-qst">Status</th></tr></thead>
              <tbody>${rows}</tbody>
            </table>
          </div>
        </div>

        <p class="cfg-foot-note">Load and rollback need the daemons to accept a whole
          configuration document, which is not wired yet — Save produces the file they will take.</p>
      </div>`;

    wire();
  }

  function wire() {
    document.getElementById('opView')?.addEventListener('click', () => {
      if (typeof window.NMS.viewRunningConfig === 'function') window.NMS.viewRunningConfig();
    });

    // Saving is a plain client-side download: the document is already in hand, and keeping it a
    // file (rather than a server-side export) means the operator can diff, archive or hand it to
    // another appliance without pretzel mediating.
    document.getElementById('opSave')?.addEventListener('click', () => {
      if (!running) return;
      const name = `pretzel-running-config-v${running.version}.json`;
      const blob = new Blob([JSON.stringify(running.config, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = name;
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
    });
  }

  // ── Init ─────────────────────────────────────────────────────────────────────
  const operationRefresh = async () => { await load(); render(); };

  function activate() {
    render();          // paint what we have, then repaint with fresh data
    operationRefresh();
    window.NMS.onRefresh(operationRefresh);
  }

  document.addEventListener('DOMContentLoaded', async () => {
    if (activeTab() !== 'operation') return;   // only this tab reads these endpoints
    await load();
    activate();
  });

  document.addEventListener('nms:tab-change', (e) => {
    if (e.detail.tab === 'operation') activate();
  });
})();
