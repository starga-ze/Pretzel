/* device-test.js — the shared test panel.
 *
 * One rendering of "the appliance is talking to a device right now", used by the API Credential,
 * API Endpoint, API Connector and SASE tests. Four pages asking the same question deserve one
 * answer on screen; the differences between them are the labels, which is what it takes.
 *
 *   NMS.testPanel.start(title, labels, note)  open the modal in the running state
 *   NMS.testPanel.runningBody(labels, note)   ... or just the markup
 *   NMS.testPanel.attachElapsed(scope)        the live counter
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;

  // ── Device-test panel (shared by the API Key, Endpoint, Connector and SASE tests) ───────────
  //
  // One renderer for all four so a test looks the same wherever it is run from, and so the running
  // state is not re-invented per page.
  //
  // On what is animated: the appliance performs the device exchange and answers the held ticket ONCE,
  // with every step's outcome at the end — while it is in flight the poll returns only "pending". So
  // there is no per-step progress to show, and pretending otherwise (ticking steps green on a timer)
  // would report success the appliance never reported. Instead every step spins together, meaning
  // "this is what is being attempted", and the one truly live figure — how long the call has been
  // outstanding — is counted up beside it. That matters here: a SASE read routinely takes ~8s and a
  // dead device runs to the timeout, and the operator can see the difference as it happens.
  // (`esc` is the module-level helper declared further down this file.)
  window.NMS.testPanel = {
    // st: undefined/null = not run, 'running' = in flight, else {ok, detail} from the appliance.
    step(label, st) {
      const mark = st === 'running' ? '<span class="ts-spin"></span>'
                 : !st ? '<span class="ts-idle">·</span>'
                 : st.ok ? '<span class="ts-ok">✓</span>'
                 : '<span class="ts-fail">✗</span>';
      return `<div class="ts-row"><span class="ts-mark">${mark}</span>
                <span class="ts-label">${esc(label)}</span>
                <span class="ts-detail">${esc((st && st !== 'running' && st.detail) || '')}</span></div>`;
    },

    // The panel body to show while the test is in flight. `note` is optional context under the steps.
    // Marked with a data attribute rather than an id: the connector page renders this inline in the
    // editor, so two panels can exist at once and an id would collide.
    runningBody(labels, note) {
      return `<div class="test-panel running">
          <div class="ts-head"><span class="ts-spin"></span>
            <span class="ts-head-label">Running</span>
            <span class="ts-elapsed" data-ts-elapsed>0.0s</span></div>
          ${labels.map((l) => this.step(l, 'running')).join('')}
          ${note ? `<div class="ts-note">${esc(note)}</div>` : ''}
        </div>`;
    },

    // Drive the elapsed counter inside `root` (default: the whole document). Returns a handle whose
    // stop() ends the counter and reports the seconds elapsed. The timer also self-cancels once its
    // element leaves the DOM, so a caller that throws before stopping cannot leave one running.
    attachElapsed(root) {
      const began = Date.now();
      const scope = root || document;
      // Per-handle, not shared: the connector page runs its panel inline, so a second test started
      // elsewhere must not freeze the first one's counter.
      let timer = setInterval(() => {
        const el = scope.querySelector('[data-ts-elapsed]');
        if (!el) { clearInterval(timer); timer = null; return; }
        el.textContent = ((Date.now() - began) / 1000).toFixed(1) + 's';
      }, 100);
      return {
        stop() {
          if (timer) { clearInterval(timer); timer = null; }
          return (Date.now() - began) / 1000;
        },
      };
    },

    // Open the modal in the running state and start the counter (the modal-based tests). The footer
    // stays the default Close: the appliance owns the in-flight exchange and there is no way to call
    // it back, so a button labelled Cancel would promise something this cannot do.
    start(title, labels, note) {
      window.NMS.modal.open(title, this.runningBody(labels, note));
      return this.attachElapsed(document);
    },
  };
})();
