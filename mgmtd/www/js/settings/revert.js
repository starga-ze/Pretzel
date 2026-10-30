/* revert.js — the other end of the candidate.
 *
 *   NMS.revertFlow({dirty, onReverted, onCancel})
 *
 * Nothing is sent. The candidate only ever existed in this browser tab, so reverting is a local
 * act — which is also why it is confirmed: there is no copy of it anywhere else.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;

  // ── Revert flow ──────────────────────────────────────────────────────────────
  // The review diff read the other way round. Publish asks "running → staged, send it?"; this
  // asks "staged → running, throw it away?" — so the panes are the same two documents with the
  // sides swapped, and the operator reads the change they are about to lose in the column it
  // already lives in.
  //
  // Confirmed rather than immediate, and the confirming button wears the danger colour, because
  // this is the only control on the page that destroys work and the only one with nothing to undo
  // it: the candidate exists nowhere but this browser tab.
  window.NMS.revertFlow = function (opts) {
    const staged = {}, running = {};
    opts.dirty.forEach(p => {
      Object.assign(staged, p.after());
      Object.assign(running, p.before());
    });

    const ov = window.NMS.modal.ensure();
    ov.querySelector('#cmTitle').textContent = 'Revert to running configuration';
    ov.querySelector('#cmBody').innerHTML = `
      <div class="cm-warn">
        <div class="cm-warn-h">These staged changes will be discarded</div>
        <ul><li>Nothing is sent to the appliance and nothing it is running changes. Only the
          edits staged in this browser tab are dropped, and every tab is re-read from the
          configuration the daemons are already on.</li></ul>
        <div class="cm-warn-note">Staged API keys and passwords are dropped with them — they were
          never sent, so they cannot be recovered and will have to be entered again.</div>
      </div>` + window.NMS.diff.html(staged, running);
    ov.querySelector('#cmFoot').innerHTML = `
      <button class="btn-sm" id="cmCancel">Cancel</button>
      <button class="btn-danger btn-sm" id="cmRevert">Revert</button>`;
    ov.querySelector('#cmCancel').onclick = () => {
      // Back where the operator came from, when they came from somewhere. Reached from the
      // topbar menu there is nowhere to go back to, so it closes.
      if (typeof opts.onCancel === 'function') opts.onCancel();
      else window.NMS.modal.close();
    };
    ov.querySelector('#cmRevert').onclick = () => {
      opts.onReverted();
      window.NMS.modal.close();
      // Repaint from the committed copy, exactly as a publish does when the reload converges and
      // as the topbar's refresh button does. Each module's own handler, so every tab reloads the
      // way it knows how — and because load() reads the draft after fetching, the drafts have to
      // be gone before this runs. onReverted() above is what clears them.
      if (typeof window.NMS._onRefresh === 'function') {
        try { window.NMS._onRefresh(); } catch (_) { /* the drafts are gone either way */ }
      }
    };
    ov.classList.add('open');
  };
})();
