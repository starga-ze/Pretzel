/* modal.js — the centre-screen dialog, and the two one-question forms of it.
 *
 * Shared infrastructure: the publish diff, the running-config viewer, the connector endpoint
 * tests and every confirmation on the Configuration page render into the SAME overlay rather
 * than each building their own. It stacks above the slide-over (z-index 600 vs 501), so an
 * editor can raise a dialog without being dismissed by it.
 *
 *   NMS.modal.open(title, bodyHtml, footHtml) -> the overlay element
 *   NMS.modal.ensure()                        -> the overlay, built but not opened
 *   NMS.modal.close()
 *   NMS.confirm({title, message, detail, confirmLabel}) -> Promise<boolean>
 *   NMS.notice({title, message, detail, okLabel})       -> Promise<void>
 *
 * It lived in commit.js, which is a file about publishing a configuration — so nine modules
 * that only wanted to ask a question had to be loaded after the publish flow to get one.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;

  function ensureModal() {
    let ov = document.getElementById('cmOverlay');
    // Reset the width every time rather than on close: the review dialog is the only wide one,
    // and a flow that opened after it used to inherit 1180px of empty space for a two-line
    // confirmation. Resetting here means no flow has to know another flow exists.
    if (ov) { ov.querySelector('.cm-dialog')?.classList.remove('cm-dialog-wide'); return ov; }
    ov = document.createElement('div');
    ov.id = 'cmOverlay';
    ov.className = 'cm-overlay';
    ov.innerHTML = `
      <div class="cm-dialog" role="dialog" aria-modal="true">
        <div class="cm-head"><span class="cm-title" id="cmTitle">Review changes</span><button class="cm-close" id="cmClose" aria-label="Close">&times;</button></div>
        <div class="cm-body" id="cmBody"></div>
        <div class="cm-foot" id="cmFoot"></div>
      </div>`;
    document.body.appendChild(ov);
    ov.addEventListener('click', (e) => { if (e.target === ov) closeModal(); });
    ov.querySelector('#cmClose').addEventListener('click', closeModal);
    return ov;
  }
  function closeModal() { document.getElementById('cmOverlay')?.classList.remove('open'); }

  // The centre-screen modal is shared infrastructure: the publish diff, the running-config
  // viewer and the connector endpoint test all render into the same element rather than each
  // building their own overlay. It stacks above the slide-over (z-index 600 vs 501), so an
  // editor can raise it without being dismissed.
  window.NMS.modal = {
    open(title, bodyHtml, footHtml) {
      const ov = ensureModal();
      ov.querySelector('#cmTitle').textContent = title;
      ov.querySelector('#cmBody').innerHTML = bodyHtml;
      ov.querySelector('#cmFoot').innerHTML = footHtml || `<button class="btn-sm" id="cmDone">Close</button>`;
      ov.querySelector('#cmDone')?.addEventListener('click', closeModal);
      ov.classList.add('open');
      return ov;
    },
    close: closeModal,
    // The overlay built but not opened, for a flow that fills it itself.
    ensure: ensureModal,
  };

  // ── Confirm ─────────────────────────────────────────────────────────────────────────────────
  //
  // The console's own dialog, in place of window.confirm. Not cosmetic: the native one is drawn by
  // the browser in the operating system's language and typography, so a destructive step — the one
  // moment the page most needs to look like it belongs to this appliance — was the one that did
  // not. It also cannot be styled, cannot carry a danger colour on the confirming button, and its
  // wording ("OK") says nothing about what is about to happen.
  //
  // Built on NMS.modal so it stacks above a slide-over the same way the review diff does, and so a
  // page that has an editor open can ask without being dismissed.
  //
  // Promise-based rather than callback: the callers are `async` already, and `if (!await ...) return;`
  // reads the way the `if (!confirm(...)) return;` it replaces did.
  // The same dialog with one button, in place of window.alert.
  //
  // For the reason NMS.confirm exists: the native one is drawn by the browser, in the operating
  // system's language and typography, with the page's own hostname above the message — so the one
  // moment the console most needs to look like this appliance was the one moment it did not.
  //
  // Promise-based like its neighbour, so a caller can await the acknowledgement; callers that do not
  // care simply do not await.
  window.NMS.notice = function (opts) {
    const o = typeof opts === 'string' ? { message: opts } : (opts || {});
    return new Promise((resolve) => {
      let settled = false;
      const done = () => {
        if (settled) return;
        settled = true;
        closeModal();
        resolve();
      };

      const body = `<p class="cf-msg">${esc(o.message || '')}</p>`
        + (o.detail ? `<p class="cf-detail">${esc(o.detail)}</p>` : '');
      const ov = window.NMS.modal.open(
        o.title || 'Notice', body,
        `<button class="btn-primary btn-sm" id="cfOk">${esc(o.okLabel || 'OK')}</button>`);

      ov.querySelector('#cfOk').addEventListener('click', done);
      ov.querySelector('#cfOk').focus();
      ov.addEventListener('click', (e) => { if (e.target === ov) done(); }, { once: true });
      ov.querySelector('#cmClose')?.addEventListener('click', done, { once: true });
      document.addEventListener('keydown', function onKey(e) {
        if (e.key !== 'Escape' && e.key !== 'Enter') return;
        document.removeEventListener('keydown', onKey);
        done();
      });
    });
  };

  window.NMS.confirm = function (opts) {
    const o = typeof opts === 'string' ? { message: opts } : (opts || {});
    return new Promise((resolve) => {
      let settled = false;
      const done = (v) => {
        if (settled) return;
        settled = true;
        closeModal();
        resolve(v);
      };

      const body = `<p class="cf-msg">${esc(o.message || 'Are you sure?')}</p>`
        + (o.detail ? `<p class="cf-detail">${esc(o.detail)}</p>` : '');
      const ov = window.NMS.modal.open(
        o.title || 'Confirm', body,
        `<button class="btn-sm" id="cfNo">${esc(o.cancelLabel || 'Cancel')}</button>
         <button class="btn-primary btn-sm${o.danger === false ? '' : ' is-danger'}" id="cfYes">${
           esc(o.confirmLabel || 'Remove')}</button>`);

      ov.querySelector('#cfNo').addEventListener('click', () => done(false));
      ov.querySelector('#cfYes').addEventListener('click', () => done(true));
      ov.querySelector('#cfYes').focus();

      // Dismissing by any other route is a no. The overlay's own backdrop click and the head's
      // close cross both run closeModal without telling us, so the promise is settled here rather
      // than left pending for the life of the page.
      ov.addEventListener('click', (e) => { if (e.target === ov) done(false); }, { once: true });
      ov.querySelector('#cmClose')?.addEventListener('click', () => done(false), { once: true });
      document.addEventListener('keydown', function onKey(e) {
        if (e.key === 'Escape') { document.removeEventListener('keydown', onKey, true); done(false); }
      }, true);
    });
  };
})();
