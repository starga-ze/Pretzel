/* diff.js — line diffs, rendered two ways.
 *
 *   NMS.diff.html(before, after)              whole-document, side by side
 *   NMS.diff.entityHtml(before, after, heads)  one entity, with gutters and folded context
 *   NMS.diff.fmtStamp(iso)
 *
 * Both are driven by the same longest-common-subsequence pass. They are apart from the flows
 * that use them because they are about TEXT, not about configuration: the review dialog and the
 * revert confirmation each render a diff, and neither should own how one is drawn.
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  const { esc } = window.NMS.utils;


  // Longest-common-subsequence diff, rendered as aligned left/right rows.
  function splitDiff(beforeStr, afterStr) {
    const a = beforeStr.split('\n'), b = afterStr.split('\n');
    const n = a.length, m = b.length;
    const dp = Array.from({ length: n + 1 }, () => new Int32Array(m + 1));
    for (let i = n - 1; i >= 0; i--)
      for (let j = m - 1; j >= 0; j--)
        dp[i][j] = a[i] === b[j] ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
    const rows = [];
    let i = 0, j = 0, remq = [], addq = [];
    const flush = () => {
      for (let k = 0; k < Math.max(remq.length, addq.length); k++)
        rows.push({ l: remq[k] ?? null, r: addq[k] ?? null, ch: true });
      remq = []; addq = [];
    };
    while (i < n && j < m) {
      if (a[i] === b[j]) { flush(); rows.push({ l: a[i], r: b[j], ch: false }); i++; j++; }
      else if (dp[i + 1][j] >= dp[i][j + 1]) remq.push(a[i++]);
      else addq.push(b[j++]);
    }
    while (i < n) remq.push(a[i++]);
    while (j < m) addq.push(b[j++]);
    flush();
    return rows;
  }

  function diffHtml(before, after) {
    const rows = splitDiff(JSON.stringify(before, null, 2), JSON.stringify(after, null, 2));
    const cell = (line, side, ch) => line == null
      ? `<div class="sd-line sd-empty"></div>`
      : `<div class="sd-line ${ch ? (side === 'l' ? 'sd-del' : 'sd-add') : ''}">${esc(line)}</div>`;
    return `
      <div class="cm-split">
        <div class="cm-split-pane"><div class="cm-split-head">Before</div>${rows.map(r => cell(r.l, 'l', r.ch)).join('')}</div>
        <div class="cm-split-pane"><div class="cm-split-head">After</div>${rows.map(r => cell(r.r, 'r', r.ch)).join('')}</div>
      </div>`;
  }


  // ── Entity diff (line numbers, folded context) ───────────────────────────────
  // The review dialog's diff, as opposed to diffHtml above: one entity rather than a whole
  // domain, so it can afford gutters and it cannot afford to print forty identical lines around
  // the one that moved. Both panes are cells of a single grid — not two scrolling columns — so a
  // line and its counterpart stay on the same row no matter how they wrap, and a fold can span
  // the full width without being drawn twice.
  const FOLD_CONTEXT = 3;   // unchanged lines kept either side of a change
  const FOLD_MIN     = 4;   // a shorter run costs less to show than a button to reveal it

  function fmtStamp(value) {
    if (!value) return null;
    const d = new Date(value);
    if (isNaN(d.getTime())) return String(value);
    return d.toLocaleString(undefined, {
      year: 'numeric', month: 'short', day: 'numeric',
      hour: '2-digit', minute: '2-digit',
    });
  }

  function entityDiffHtml(before, after, heads) {
    const bStr = before === undefined ? null : JSON.stringify(before, null, 2);
    const aStr = after === undefined ? null : JSON.stringify(after, null, 2);

    // An added or removed entity has one side that does not exist — which is not the same as a
    // side that is empty, and is why this does not go through splitDiff. An empty string splits
    // into one empty line, so an added site used to open with a phantom red row deleting a line
    // that was never there, directly under a header saying nothing had been published.
    let rows;
    if (bStr === null || aStr === null) {
      const lines = (bStr === null ? aStr : bStr).split('\n');
      rows = lines.map(line => (bStr === null
        ? { l: null, r: line, ch: true }
        : { l: line, r: null, ch: true }));
    } else {
      // splitDiff is an LCS over an (n+1)×(m+1) Int32Array, so its memory is the product of the
      // two line counts. An entity is one site or one rule and never comes near this; the cap is
      // here so that the day something does, the dialog renders both sides plainly instead of
      // allocating hundreds of megabytes in the operator's tab and taking the browser with it.
      const bl = bStr.split('\n'), al = aStr.split('\n');
      if (bl.length * al.length > 4000000) {
        const n = Math.max(bl.length, al.length);
        rows = [];
        for (let k = 0; k < n; k++)
          rows.push({ l: bl[k] ?? null, r: al[k] ?? null, ch: bl[k] !== al[k] });
      } else {
        rows = splitDiff(bStr, aStr);
      }
    }

    let ln = 0, rn = 0;
    rows.forEach(r => {
      r.ln = r.l == null ? null : ++ln;
      r.rn = r.r == null ? null : ++rn;
    });

    // A row is kept when it changed or sits within FOLD_CONTEXT of something that did.
    const keep = new Array(rows.length).fill(false);
    rows.forEach((r, i) => {
      if (!r.ch) return;
      for (let j = Math.max(0, i - FOLD_CONTEXT); j <= Math.min(rows.length - 1, i + FOLD_CONTEXT); j++)
        keep[j] = true;
    });

    const cell = (line, num, side, ch) => {
      if (line == null)
        return `<div class="rv-line rv-void"><span class="rv-num"></span><span class="rv-code"></span></div>`;
      const mark = ch ? (side === 'l' ? ' rv-del' : ' rv-add') : '';
      return `<div class="rv-line${mark}"><span class="rv-num">${num == null ? '' : num}</span>`
           + `<span class="rv-code">${esc(line)}</span></div>`;
    };
    const pair = (r) => cell(r.l, r.ln, 'l', r.ch) + cell(r.r, r.rn, 'r', r.ch);

    let html = '';
    let i = 0;
    while (i < rows.length) {
      if (keep[i]) { html += pair(rows[i]); i++; continue; }

      let j = i;
      while (j < rows.length && !keep[j]) j++;
      const run = rows.slice(i, j);
      if (run.length < FOLD_MIN) {
        run.forEach(r => { html += pair(r); });
      } else {
        html += `<button class="rv-fold-btn" type="button" data-fold>`
              + `Expand ${run.length} unchanged row${run.length > 1 ? 's' : ''}`
              + `<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor"`
              + ` stroke-width="3"><polyline points="6 9 12 15 18 9"/></svg></button>`
              // display:contents via the class, so these cells rejoin the parent grid when shown
              // and the fold is one `display:none` rather than a row-by-row toggle.
              + `<div class="rv-fold is-folded">${run.map(pair).join('')}</div>`;
      }
      i = j;
    }

    return `<div class="rv-diff">
        <div class="rv-diff-head">${esc(heads.left)}</div>
        <div class="rv-diff-head">${esc(heads.right)}</div>
        ${html}
      </div>`;
  }

  window.NMS.diff = {
    html: diffHtml,
    entityHtml: entityDiffHtml,
    fmtStamp,
  };
})();
