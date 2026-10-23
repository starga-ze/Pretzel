/* laboratory.js — Laboratory ▸ file download / upload sandbox.
 *
 * Minimal reproduction of the customer download flow:
 *
 *   click Download → a PDF of records is built entirely client-side and saved through
 *   the native download manager (blob + <a download>).
 *
 * No password dialog, no page navigation, nothing rendered on the page — the records
 * exist only inside the downloaded PDF. Supply your own rows via LAB_RECORDS below.
 *
 * OOP: small single-responsibility classes wired by a LaboratoryPage controller. */
(function () {
  'use strict';

  // ════════════════════════════════════════════════════════════════════════════
  // Records source — EDIT HERE.
  // Put your own record rows in this array (one string per row). When it is empty,
  // synthetic rows are generated from the "Records" count selector instead. Records
  // go into the downloaded PDF only — nothing is rendered on the page.
  // ════════════════════════════════════════════════════════════════════════════
  const LAB_RECORDS = [
    // '#1  Karrot User 1000  010-1000-0000  CI=...',
    // '#2  Karrot User 1001  010-1001-7919  CI=...',
  ];

  // localStorage key for the "fire on every page load" external-app-launch switch, and how
  // long after load the stored launch fires.
  const AUTO_LAUNCH_KEY = 'labExternalAutoLaunch';
  const AUTO_LAUNCH_DELAY_MS = 1000;

  // Scheme scan list for "which app owns which scheme". Secure-browser vendors do not
  // document their private schemes, so these are candidates to be confirmed by the probe —
  // the unregistered control at the end shows what "nothing is registered" looks like.
  // google-chrome: is deliberately absent: it opens Chrome on most desktops.
  const CANDIDATE_SCHEMES = [
    { scheme: 'prisma-access-browser', note: 'Prisma Access Browser — candidate' },
    { scheme: 'prismaaccessbrowser',   note: 'Prisma Access Browser — candidate' },
    { scheme: 'prisma-access',         note: 'Prisma Access — candidate' },
    { scheme: 'pab',                   note: 'Prisma Access Browser — candidate' },
    { scheme: 'talon',                 note: 'Talon (pre-acquisition name) — candidate' },
    { scheme: 'com-okta-authenticator', note: 'Okta Verify — known app scheme' },
    { scheme: 'msteams',               note: 'Microsoft Teams — known app scheme' },
    { scheme: 'pretzel-lab-noapp',     note: 'control — intentionally unregistered' },
  ];

  // Repro matrix — one controlled attempt per hypothesis, each with a scheme that makes its
  // console row unmistakable. Only one variable changes between neighbouring cases, so the
  // verdict column alone tells you which condition produced it. The case that opens a dialog
  // goes last: a tab-modal dialog would swallow whatever is fired behind it.
  const REPRO_MATRIX = [
    { id: 'A', scheme: 'pretzel-fresh', shape: 'bare', unique: true,
      expect: 'never-seen scheme, no registered app — expect permissionRequested with NO dialog ' +
              '(this is the customer case: External Application = None)' },
    { id: 'B', scheme: 'google-chrome', shape: 'bare',
      expect: 'the exact customer URL — expect allowed if a decision is remembered for this origin, ' +
              'permissionRequested if not' },
    { id: 'C', scheme: 'google-chrome', shape: 'custom', pathProbe: true,
      expect: 'host + path + query — shows exactly where the log truncates externalApp.url' },
    { id: 'E', scheme: 'google-chrome', shape: 'opaque', pathProbe: true,
      expect: 'payload after the colon with NO "//" — if the log still shows a plain ' +
              '"google-chrome:", then a bare-looking customer log cannot prove the call was bare' },
    { id: 'D', scheme: 'msteams', shape: 'bare',
      expect: 'a scheme with a real registered app and (probably) no remembered decision — ' +
              'expect permissionRequested WITH a visible dialog' },
  ];

  // ── Shared helpers ──────────────────────────────────────────────────────────
  function escHtml(s) {
    return String(s ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function formatBytes(n) {
    if (!Number.isFinite(n) || n < 0) return '--';
    const units = ['B', 'KB', 'MB', 'GB'];
    let i = 0, v = n;
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
    return `${i === 0 ? v : v.toFixed(1)} ${units[i]}`;
  }

  // ════════════════════════════════════════════════════════════════════════════
  // ActivityLog — timestamped event console.
  // ════════════════════════════════════════════════════════════════════════════
  class ActivityLog {
    constructor(el) { this.el = el; }

    log(tag, message, kind = '') {
      if (!this.el) return;
      const now = new Date();
      const ts = now.toLocaleTimeString('en-GB', { hour12: false }) +
        '.' + String(now.getMilliseconds()).padStart(3, '0');
      const line = document.createElement('div');
      line.className = 'lab-log-line' + (kind ? ` evt-${kind}` : '');
      line.innerHTML =
        `<span class="lab-log-time">${ts}</span>` +
        `<span class="lab-log-tag">${escHtml(tag)}</span>` +
        `<span class="lab-log-msg">${escHtml(message)}</span>`;
      this.el.appendChild(line);
      this.el.scrollTop = this.el.scrollHeight;
    }

    info(msg)   { this.log('INFO', msg); }
    up(msg)     { this.log('UPLOAD', msg, 'up'); }
    down(msg)   { this.log('DOWNLOAD', msg, 'down'); }
    launch(msg) { this.log('LAUNCH', msg, 'launch'); }
    error(msg)  { this.log('ERROR', msg, 'error'); }

    clear() { if (this.el) this.el.innerHTML = ''; }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // FileDownloader — save a Blob through the browser's native download manager via an
  // <a download> click. Uses the download attribute, so it never navigates the page.
  // ════════════════════════════════════════════════════════════════════════════
  class FileDownloader {
    // Blob URL download: the <a> href is blob:https://host/<uuid>.
    saveBlob(blob, filename) {
      const url = URL.createObjectURL(blob);
      this._clickAnchor(url, filename);
      setTimeout(() => URL.revokeObjectURL(url), 1000);
      return blob.size;
    }

    // Data URL download: the <a> href is data:<mime>;base64,… — NO blob: prefix, which
    // matches the customer's observed download URL. Async (FileReader), so the click
    // lands a tick later. Note: very large payloads can exceed data-URL limits.
    saveDataUrl(blob, filename) {
      const reader = new FileReader();
      reader.onload = () => this._clickAnchor(reader.result, filename);
      reader.readAsDataURL(blob);
      return blob.size;
    }

    _clickAnchor(href, filename) {
      const a = document.createElement('a');
      a.href = href;
      a.download = filename || 'download.bin';
      a.rel = 'noopener';
      document.body.appendChild(a);
      a.click();
      a.remove();
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // PdfGenerator — builds a minimal, valid, uncompressed PDF from synthetic records,
  // entirely client-side (no server round-trip). Records embed 010 phone numbers and a
  // "PII included" marker so the payload matches the DLP data profile as plain text.
  // ════════════════════════════════════════════════════════════════════════════
  class PdfGenerator {
    // Build a PDF Blob from explicit record lines.
    build(lines) {
      return this._buildPdf(lines);
    }

    // Synthetic records, used when no custom LAB_RECORDS list is supplied.
    records(n, title) {
      const lines = [title, 'PII included', ''];
      for (let i = 0; i < n; i++) {
        const phone = '010-' + String(1000 + (i % 9000)).padStart(4, '0') +
                      '-' + String((i * 7919) % 10000).padStart(4, '0');
        lines.push(`#${i + 1}  Karrot User ${1000 + i}  ${phone}  CI=${this._token(48, i)}`);
      }
      return lines;
    }

    _token(len, seed) {
      let x = (seed * 2654435761) >>> 0, s = '';
      const h = '0123456789abcdef';
      for (let i = 0; i < len; i++) { x = (x * 1103515245 + 12345) >>> 0; s += h[(x >>> 8) & 15]; }
      return s;
    }

    _buildPdf(lines) {
      const TOP = 800, LEFT = 40, LEAD = 12, FONT = 10, MINY = 40;
      const perPage = Math.max(1, Math.floor((TOP - MINY) / LEAD));

      const pages = [];
      for (let i = 0; i < lines.length; i += perPage) pages.push(lines.slice(i, i + perPage));
      if (!pages.length) pages.push(['']);

      const esc = (s) => String(s).replace(/\\/g, '\\\\').replace(/\(/g, '\\(')
                          .replace(/\)/g, '\\)').replace(/[^\x20-\x7e]/g, '?');

      const N = pages.length;
      const obj = [];
      obj[1] = '<< /Type /Catalog /Pages 2 0 R >>';
      const kids = [];
      for (let p = 0; p < N; p++) kids.push((5 + 2 * p) + ' 0 R');
      obj[2] = '<< /Type /Pages /Kids [' + kids.join(' ') + '] /Count ' + N + ' >>';
      obj[3] = '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>';

      for (let p = 0; p < N; p++) {
        const contentNum = 4 + 2 * p, pageNum = 5 + 2 * p;
        let stream = 'BT /F1 ' + FONT + ' Tf ' + LEAD + ' TL ' + LEFT + ' ' + TOP + ' Td\n';
        pages[p].forEach((ln, idx) => {
          stream += (idx === 0 ? '' : 'T* ') + '(' + esc(ln) + ') Tj\n';
        });
        stream += 'ET';
        obj[contentNum] = '<< /Length ' + stream.length + ' >>\nstream\n' + stream + '\nendstream';
        obj[pageNum] = '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] ' +
          '/Resources << /Font << /F1 3 0 R >> >> /Contents ' + contentNum + ' 0 R >>';
      }

      const maxNum = 3 + 2 * N;
      let pdf = '%PDF-1.4\n';
      const offsets = new Array(maxNum + 1).fill(0);
      for (let n = 1; n <= maxNum; n++) {
        offsets[n] = pdf.length;
        pdf += n + ' 0 obj\n' + obj[n] + '\nendobj\n';
      }
      const xrefStart = pdf.length;
      pdf += 'xref\n0 ' + (maxNum + 1) + '\n0000000000 65535 f \n';
      for (let n = 1; n <= maxNum; n++)
        pdf += String(offsets[n]).padStart(10, '0') + ' 00000 n \n';
      pdf += 'trailer\n<< /Size ' + (maxNum + 1) + ' /Root 1 0 R >>\nstartxref\n' + xrefStart + '\n%%EOF';

      return new Blob([pdf], { type: 'application/pdf' });
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // CanvasPdfGenerator — a genuinely canvas-based download. Records are painted onto a
  // canvas, the canvas is rasterized to JPEG, and that image is wrapped in a single-page
  // PDF (DCTDecode). Unlike the text PDF, the file's content is PIXELS, not selectable
  // text — so DLP text extraction cannot read the records out of the downloaded file.
  // ════════════════════════════════════════════════════════════════════════════
  class CanvasPdfGenerator {
    generate(lines) {
      const W = 794, H = 1123;                          // A4 @ ~96 dpi
      const canvas = document.createElement('canvas');
      canvas.width = W; canvas.height = H;
      const ctx = canvas.getContext('2d');
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, W, H);
      ctx.fillStyle = '#111111';
      ctx.font = '13px ui-monospace, monospace';
      const pad = 32, lh = 15;
      let y = pad + 6;
      for (const ln of lines) {
        if (y > H - pad) break;                         // one page's worth
        ctx.fillText(ln, pad, y);
        y += lh;
      }
      const jpeg = this._dataUrlToBytes(canvas.toDataURL('image/jpeg', 0.85));
      return this._imagePdf(jpeg, W, H, 595, 842);      // image px + A4 point size
    }

    _dataUrlToBytes(dataUrl) {
      const bin = atob(dataUrl.split(',')[1]);
      const bytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
      return bytes;
    }

    // Assemble a single-page image PDF. Built as mixed string + Uint8Array parts so the
    // JPEG bytes survive (a plain JS string would be mangled by the Blob's UTF-8 encode);
    // byte offsets are tracked across parts for a correct xref table.
    _imagePdf(jpeg, imgW, imgH, wPt, hPt) {
      const parts = [];
      let pos = 0;
      const off = {};
      const push = (s) => { parts.push(s); pos += s.length; };   // ASCII strings & Uint8Array

      push('%PDF-1.4\n');
      off[1] = pos; push('1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n');
      off[2] = pos; push('2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n');
      off[3] = pos; push('3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ' + wPt + ' ' + hPt +
        '] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\nendobj\n');
      const content = 'q ' + wPt + ' 0 0 ' + hPt + ' 0 0 cm /Im0 Do Q';
      off[4] = pos; push('4 0 obj\n<< /Length ' + content.length + ' >>\nstream\n' + content + '\nendstream\nendobj\n');
      off[5] = pos;
      push('5 0 obj\n<< /Type /XObject /Subtype /Image /Width ' + imgW + ' /Height ' + imgH +
        ' /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length ' + jpeg.length + ' >>\nstream\n');
      push(jpeg);
      push('\nendstream\nendobj\n');

      const xrefStart = pos;
      let xref = 'xref\n0 6\n0000000000 65535 f \n';
      for (let n = 1; n <= 5; n++) xref += String(off[n]).padStart(10, '0') + ' 00000 n \n';
      push(xref);
      push('trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n' + xrefStart + '\n%%EOF');

      return new Blob(parts, { type: 'application/pdf' });
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // RecordPreview — optional on-page preview of the records, either as real DOM text
  // (non-canvas) or painted on a <canvas>. The two exist for A/B comparison: a
  // content-based capture/masking engine can read the DOM version but not the canvas
  // one (pixels, no DOM). Default is no preview at all.
  // ════════════════════════════════════════════════════════════════════════════
  class RecordPreview {
    clear(host) { if (host) { host.innerHTML = ''; host.style.display = 'none'; } }

    renderDom(host, lines) {
      host.style.display = 'block';
      const pre = document.createElement('pre');
      pre.className = 'lab-preview-dom';
      pre.textContent = lines.slice(0, 400).join('\n');
      host.innerHTML = '';
      host.appendChild(pre);
    }

    // Visible canvas preview.
    renderCanvas(host, lines) {
      host.style.display = 'block';
      host.innerHTML = '';
      const canvas = document.createElement('canvas');
      host.appendChild(canvas);
      this._draw(canvas, lines, host.getBoundingClientRect().width || 760);
    }

    // Canvas is rendered (drawn to a real 2D context) but its host stays hidden, so
    // nothing shows on the page — a canvas render with no visible preview.
    renderCanvasHidden(host, lines) {
      host.innerHTML = '';
      host.style.display = 'none';
      const canvas = document.createElement('canvas');
      host.appendChild(canvas);
      this._draw(canvas, lines, 760);
    }

    _draw(canvas, lines, cssW) {
      const dpr = window.devicePixelRatio || 1;
      const pad = 14, headerH = 34, lh = 15;
      const shown = lines.slice(0, 400);
      const cssH = Math.min(600, headerH + shown.length * lh + pad);
      canvas.style.height = cssH + 'px';
      canvas.width  = Math.floor(cssW * dpr);
      canvas.height = Math.floor(cssH * dpr);

      const ctx = canvas.getContext('2d');
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, cssW, cssH);
      ctx.fillStyle = '#374151';
      ctx.font = '11px ui-monospace, monospace';
      let y = headerH;
      for (const ln of shown) {
        if (y > cssH - pad) break;
        ctx.fillText(ln, pad, y);
        y += lh;
      }
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // CanvasDialog — the confirm dialog itself, drawn entirely on a <canvas> (title, file
  // box, buttons are all ctx.fillText/fillRect, no DOM). Buttons are handled by
  // hit-testing canvas click coordinates. This mimics a Canvas-based application's UI:
  // the masking engine has no DOM to read, so the dialog's contents can't be masked.
  // ════════════════════════════════════════════════════════════════════════════
  class CanvasDialog {
    constructor({ onConfirm }) {
      this.onConfirm = onConfirm || (() => {});
      this.meta = null;
      this.W = 440; this.H = 236;
      this.btns = {
        download: { x: this.W - 132, y: this.H - 54, w: 104, h: 36, label: 'Download' },
        cancel:   { x: this.W - 244, y: this.H - 54, w: 100, h: 36, label: 'Cancel' },
      };
      this._build();
    }

    _build() {
      const overlay = document.createElement('div');
      overlay.className = 'lab-cdlg-overlay';
      const canvas = document.createElement('canvas');
      canvas.className = 'lab-cdlg-canvas';
      overlay.appendChild(canvas);
      document.body.appendChild(overlay);
      this.overlay = overlay;
      this.canvas = canvas;

      canvas.addEventListener('click', (e) => this._onClick(e));
      canvas.addEventListener('mousemove', (e) => {
        const p = this._pos(e);
        canvas.style.cursor = (this._hit(p, this.btns.download) || this._hit(p, this.btns.cancel))
          ? 'pointer' : 'default';
      });
      overlay.addEventListener('click', (e) => { if (e.target === overlay) this.close(); });
      document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && this.isOpen()) this.close(); });
    }

    isOpen() { return this.overlay.classList.contains('open'); }
    open(meta) { this.meta = meta; this._draw(); this.overlay.classList.add('open'); }
    close() { this.overlay.classList.remove('open'); this.meta = null; }

    _pos(e) {
      const r = this.canvas.getBoundingClientRect();
      return { x: (e.clientX - r.left) * (this.W / r.width),
               y: (e.clientY - r.top) * (this.H / r.height) };
    }
    _hit(p, b) { return p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h; }

    _onClick(e) {
      const p = this._pos(e);
      if (this._hit(p, this.btns.download)) { const m = this.meta; this.close(); this.onConfirm(m); }
      else if (this._hit(p, this.btns.cancel)) this.close();
    }

    _draw() {
      const dpr = window.devicePixelRatio || 1;
      this.canvas.style.width  = this.W + 'px';
      this.canvas.style.height = this.H + 'px';
      this.canvas.width  = Math.floor(this.W * dpr);
      this.canvas.height = Math.floor(this.H * dpr);
      const ctx = this.canvas.getContext('2d');
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

      ctx.fillStyle = '#ffffff'; this._rr(ctx, 0, 0, this.W, this.H, 14); ctx.fill();

      ctx.fillStyle = '#111827'; ctx.font = '700 16px -apple-system, system-ui, sans-serif';
      ctx.fillText('CI/DI download', 22, 34);
      ctx.fillStyle = '#6b7280'; ctx.font = '12px -apple-system, system-ui, sans-serif';
      ctx.fillText('Builds a PDF from the selected records.', 22, 56);

      ctx.fillStyle = '#f7f8fc'; this._rr(ctx, 22, 72, this.W - 44, 54, 9); ctx.fill();
      ctx.strokeStyle = '#e4e7f0'; this._rr(ctx, 22, 72, this.W - 44, 54, 9); ctx.stroke();
      ctx.fillStyle = '#111827'; ctx.font = '600 13px -apple-system, system-ui, sans-serif';
      ctx.fillText('Identity verification · ' + (this.meta ? this.meta.name : ''), 34, 96);
      ctx.fillStyle = '#6b7280'; ctx.font = '11px ui-monospace, monospace';
      ctx.fillText((this.meta ? this.meta.count.toLocaleString() : '0') + ' records · PDF', 34, 114);

      this._btn(ctx, this.btns.cancel, false);
      this._btn(ctx, this.btns.download, true);
    }

    _btn(ctx, b, primary) {
      ctx.fillStyle = primary ? '#f97316' : '#f7f8fc';
      this._rr(ctx, b.x, b.y, b.w, b.h, 8); ctx.fill();
      if (!primary) { ctx.strokeStyle = '#e4e7f0'; this._rr(ctx, b.x, b.y, b.w, b.h, 8); ctx.stroke(); }
      ctx.fillStyle = primary ? '#ffffff' : '#111827';
      ctx.font = '600 13px -apple-system, system-ui, sans-serif';
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(b.label, b.x + b.w / 2, b.y + b.h / 2 + 1);
      ctx.textAlign = 'start'; ctx.textBaseline = 'alphabetic';
    }

    _rr(ctx, x, y, w, h, r) {
      ctx.beginPath();
      ctx.moveTo(x + r, y);
      ctx.arcTo(x + w, y, x + w, y + h, r);
      ctx.arcTo(x + w, y + h, x, y + h, r);
      ctx.arcTo(x, y + h, x, y, r);
      ctx.arcTo(x, y, x + w, y, r);
      ctx.closePath();
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // CanvasApp — the WHOLE page rendered as one full-viewport <canvas> (like a real
  // Canvas-based application). Header, record list and the Download button are all
  // painted pixels; the button is hit-tested. Nothing here is DOM, so a content-based
  // capture/masking engine has no tree to read — the entire screen is opaque to it.
  // ════════════════════════════════════════════════════════════════════════════
  class CanvasApp {
    constructor({ records, onDownload }) {
      this.getRecords = records;
      this.onDownload = onDownload || (() => {});
      this.dlBtn = null;
      this.exitBtn = null;
      this._build();
    }

    _build() {
      const canvas = document.createElement('canvas');
      canvas.id = 'labAppCanvas';
      canvas.style.cssText = 'position:fixed;inset:0;z-index:900;display:none;background:#fff';
      document.body.appendChild(canvas);
      this.canvas = canvas;

      canvas.addEventListener('click', (e) => this._onClick(e));
      canvas.addEventListener('mousemove', (e) => {
        const p = this._pos(e);
        canvas.style.cursor = (this._in(p, this.dlBtn) || this._in(p, this.exitBtn)) ? 'pointer' : 'default';
      });
      window.addEventListener('resize', () => { if (this.isOpen()) this._draw(); });
    }

    isOpen() { return this.canvas.style.display === 'block'; }
    open()   { this.canvas.style.display = 'block'; this._draw(); }
    close()  { this.canvas.style.display = 'none'; }

    _pos(e) { const r = this.canvas.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; }
    _in(p, b) { return b && p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h; }

    _onClick(e) {
      const p = this._pos(e);
      if (this._in(p, this.dlBtn)) this.onDownload();
      else if (this._in(p, this.exitBtn)) {
        this.close();
        const cb = document.getElementById('dlPageCanvas');
        if (cb) cb.checked = false;
      }
    }

    _draw() {
      const dpr = window.devicePixelRatio || 1;
      const W = window.innerWidth, H = window.innerHeight;
      this.canvas.width  = Math.floor(W * dpr);
      this.canvas.height = Math.floor(H * dpr);
      const ctx = this.canvas.getContext('2d');
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

      ctx.fillStyle = '#f0f2f8'; ctx.fillRect(0, 0, W, H);

      ctx.fillStyle = '#111827'; ctx.font = '700 18px -apple-system, system-ui, sans-serif';
      ctx.fillText('Laboratory — canvas app (the whole page is a canvas)', 28, 42);

      this.exitBtn = { x: W - 96, y: 24, w: 68, h: 30 };
      ctx.fillStyle = '#f7f8fc'; this._rr(ctx, this.exitBtn.x, this.exitBtn.y, this.exitBtn.w, this.exitBtn.h, 8); ctx.fill();
      ctx.strokeStyle = '#e4e7f0'; ctx.stroke();
      this._label(ctx, 'Close', this.exitBtn, '#111827');

      const cardX = 28, cardY = 72, cardW = Math.min(920, W - 56), cardH = H - 104;
      ctx.fillStyle = '#ffffff'; this._rr(ctx, cardX, cardY, cardW, cardH, 12); ctx.fill();
      ctx.strokeStyle = '#e4e7f0'; ctx.stroke();

      this.dlBtn = { x: cardX + 24, y: cardY + 22, w: 150, h: 38 };
      ctx.fillStyle = '#f97316'; this._rr(ctx, this.dlBtn.x, this.dlBtn.y, this.dlBtn.w, this.dlBtn.h, 8); ctx.fill();
      this._label(ctx, 'Download…', this.dlBtn, '#ffffff', '700 14px');

      const lines = this.getRecords() || [];
      ctx.fillStyle = '#374151'; ctx.font = '11px ui-monospace, monospace';
      let y = cardY + 92; const lh = 15;
      for (const ln of lines) {
        if (y > cardY + cardH - 16) break;
        ctx.fillText(ln, cardX + 24, y);
        y += lh;
      }
    }

    _label(ctx, text, b, color, font) {
      ctx.fillStyle = color;
      ctx.font = (font || '600 13px') + ' -apple-system, system-ui, sans-serif';
      ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(text, b.x + b.w / 2, b.y + b.h / 2 + 1);
      ctx.textAlign = 'start'; ctx.textBaseline = 'alphabetic';
    }

    _rr(ctx, x, y, w, h, r) {
      ctx.beginPath();
      ctx.moveTo(x + r, y);
      ctx.arcTo(x + w, y, x + w, y + h, r);
      ctx.arcTo(x + w, y + h, x, y + h, r);
      ctx.arcTo(x, y + h, x, y, r);
      ctx.arcTo(x, y, x + w, y, r);
      ctx.closePath();
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // UploadedFile / FileUploader — local file selection (never sent to the server).
  // ════════════════════════════════════════════════════════════════════════════
  class UploadedFile {
    constructor(file) {
      this.file = file;
      this.name = file.name;
      this.size = file.size;
      this.type = file.type || 'application/octet-stream';
      this.id = 'f_' + Math.random().toString(36).slice(2, 9);
    }
    download(downloader) { return downloader.saveBlob(this.file, this.name); }
    describe() { return `${formatBytes(this.size)} · ${this.type}`; }
  }

  class FileUploader {
    constructor({ dropzone, input, onAdd }) {
      this.dropzone = dropzone;
      this.input = input;
      this.onAdd = onAdd || (() => {});
      this.files = [];
      this._bind();
    }

    _bind() {
      const dz = this.dropzone, input = this.input;
      if (input) {
        input.addEventListener('change', () => {
          this._accept(input.files);
          input.value = '';
        });
      }
      if (dz) {
        dz.addEventListener('click', () => input && input.click());
        dz.addEventListener('keydown', (e) => {
          if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); input && input.click(); }
        });
        ['dragenter', 'dragover'].forEach(ev =>
          dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.add('dragover'); }));
        ['dragleave', 'drop'].forEach(ev =>
          dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.remove('dragover'); }));
        dz.addEventListener('drop', (e) => {
          if (e.dataTransfer && e.dataTransfer.files) this._accept(e.dataTransfer.files);
        });
      }
    }

    _accept(fileList) {
      const added = [];
      for (const f of fileList) {
        const model = new UploadedFile(f);
        this.files.push(model);
        added.push(model);
      }
      if (added.length) this.onAdd(added);
    }

    find(id) { return this.files.find(f => f.id === id); }
    count()  { return this.files.length; }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // BrowserIdentity — what a page can tell about the browser it runs in.
  //
  // The common "open in Chrome" redirect pattern keys off navigator.userAgentData.brands,
  // which is exactly where a Chromium-based secure browser differs from Google Chrome:
  // the UA string still says Chrome/<n>, but "Google Chrome" is missing from brands, so
  // such sniffing code decides "not Chrome" and fires a google-chrome: navigation.
  // ════════════════════════════════════════════════════════════════════════════
  class BrowserIdentity {
    brands() {
      const uad = navigator.userAgentData;
      return (uad && Array.isArray(uad.brands)) ? uad.brands : [];
    }

    brandNames() { return this.brands().map(b => b.brand); }

    isGoogleChrome() { return this.brandNames().some(b => b === 'Google Chrome'); }
    isChromium()     { return this.brandNames().some(b => /chromium/i.test(b)); }

    lines() {
      const uad = navigator.userAgentData;
      const names = this.brandNames();
      return [
        `userAgent                  : ${navigator.userAgent}`,
        `userAgentData.brands       : ${names.length ? names.join(' | ') : '(unavailable — non-Chromium or insecure context)'}`,
        `brands has "Google Chrome" : ${this.isGoogleChrome() ? 'yes' : 'no'}`,
        `brands has "Chromium"      : ${this.isChromium() ? 'yes' : 'no'}`,
        `platform                   : ${(uad && uad.platform) || navigator.platform || '--'}`,
        `vendor                     : ${navigator.vendor || '--'}`,
      ];
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // LaunchProbe — after a scheme navigation, watch for the side effects that mean an
  // external handler actually took over: the page loses focus, or goes hidden. Nothing
  // firing inside the window means the browser dropped the navigation silently — either
  // no app is registered for the scheme, or it was blocked without a prompt.
  // ════════════════════════════════════════════════════════════════════════════
  class LaunchProbe {
    constructor(windowMs = 1500) { this.windowMs = windowMs; }

    watch(label, onVerdict) {
      const events = [];
      let settled = false;

      const mark = (name) => { events.push(name); if (!settled) { settled = true; finish(); } };
      const onBlur = () => mark('blur');
      const onVis  = () => mark(`visibilitychange→${document.visibilityState}`);
      const onHide = () => mark('pagehide');

      const finish = () => {
        clearTimeout(timer);
        window.removeEventListener('blur', onBlur);
        document.removeEventListener('visibilitychange', onVis);
        window.removeEventListener('pagehide', onHide);
        onVerdict(events.length
          ? `probe [${label}] — ${events.join(', ')} → a handler most likely opened`
          : `probe [${label}] — no blur/visibility change in ${this.windowMs} ms → nothing reacted ` +
            '(no registered handler, or dropped without a prompt)');
      };

      const timer = setTimeout(finish, this.windowMs);
      window.addEventListener('blur', onBlur);
      document.addEventListener('visibilitychange', onVis);
      window.addEventListener('pagehide', onHide);
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // ExternalAppLauncher — navigate to a non-http(s) scheme so the browser enters its
  // external-protocol handling path. Each trigger differs in how the navigation is
  // initiated, which is what decides whether the browser shows its "Open <app>?" dialog
  // or drops the attempt silently (user activation, iframe origin, repeat throttling).
  // ════════════════════════════════════════════════════════════════════════════
  class ExternalAppLauncher {
    constructor({ log, probe }) {
      this.log = log;
      this.probe = probe || (() => {});     // probe(tag) — no-op when probing is off
    }

    // scheme + shape → the exact string handed to the navigation.
    buildUrl({ scheme, shape, target }) {
      const s = String(scheme || '').trim().replace(/:+$/, '').replace(/\/+$/, '') || 'google-chrome';
      const strip = (u) => String(u || '').replace(/^[a-z0-9+.-]+:\/\//i, '');
      if (shape === 'opaque')  return `${s}:${strip(target).replace(/^\/+/, '') || 'probe'}`;
      if (shape === 'slashes') return `${s}://`;
      if (shape === 'current') return `${s}://${strip(location.href)}`;
      if (shape === 'custom')  return `${s}://${strip(target)}`;
      return `${s}:`;                        // bare scheme — what the field logs showed
    }

    // A scheme nobody has seen before. Two things key off the exact scheme string: the
    // browser's remembered "always allow <origin> to open this" decision, which is stored per
    // (origin, scheme) pair and silences the prompt once set, and console-side de-duplication
    // of identical events. A fresh scheme per attempt sidesteps both, so every attempt is
    // judged from scratch and lands in the event log as its own row.
    uniqueScheme(base) {
      return `${String(base || 'pretzel-lab').replace(/:+$/, '')}-${this._token()}`;
    }

    // Same scheme, new URL: appended after the colon for an opaque scheme-only URL, as a path
    // segment otherwise. Keeps a scheme that really has a registered handler — the only kind
    // that can raise a dialog — while still making every attempt a distinct event.
    withPathToken(url) {
      const t = `lab-${this._token()}`;
      if (/:$/.test(url))   return `${url}${t}`;
      if (/\/\/$/.test(url)) return `${url}${t}`;
      return `${url.replace(/\/+$/, '')}/${t}`;
    }

    _token() { return Math.random().toString(36).slice(2, 7); }

    // Fire `times` attempts, `interval` ms apart. `uniq` decides whether each attempt reuses
    // the prebuilt URL, gets a path token, or gets a brand-new scheme.
    launch({ method, url, delay, times, interval, uniq, scheme, shape, target }) {
      const n = Math.max(1, parseInt(times, 10) || 1);
      const gap = Math.max(0, parseInt(interval, 10) || 0);
      for (let i = 0; i < n; i++) {
        const suffix = n > 1 ? ` ${i + 1}/${n}` : '';
        setTimeout(() => this.attempt(method, this.freshUrl(url, { uniq, scheme, shape, target }),
          delay, suffix), i * gap);
      }
    }

    freshUrl(url, { uniq, scheme, shape, target }) {
      if (uniq === 'scheme') return this.buildUrl({ scheme: this.uniqueScheme(scheme), shape, target });
      if (uniq === 'path')   return this.withPathToken(url);
      return url;
    }

    attempt(method, url, delay, suffix = '', probeFn = null) {
      const tag = `${method}${suffix}`;
      const probe = probeFn || this.probe;
      const wait = Math.max(0, parseInt(delay, 10) || 0);
      try {
        switch (method) {
          case 'gesture':
            this.log.launch(`${tag} → location.href = "${url}" (inside the click handler)`);
            probe(tag);
            location.href = url;
            break;

          case 'anchor':
            this.log.launch(`${tag} → <a href="${url}"> .click() (inside the click handler)`);
            probe(tag);
            this._clickAnchor(url);
            break;

          case 'synth-anchor':
            this.log.launch(`${tag} → a.click() deferred to setTimeout(0) — not a user-initiated task`);
            setTimeout(() => { probe(tag); this._clickAnchor(url); }, 0);
            break;

          case 'timer':
            this.log.launch(`${tag} → location.href after ${wait} ms (transient activation expires at ~5000 ms)`);
            setTimeout(() => { probe(tag); location.href = url; }, wait);
            break;

          case 'iframe':
            this.log.launch(`${tag} → hidden iframe src = "${url}"`);
            probe(tag);
            this._iframe(url);
            break;

          case 'window-open': {
            this.log.launch(`${tag} → window.open("${url}", "_blank")`);
            probe(tag);
            const w = window.open(url, '_blank');
            this.log.info(`window.open returned ${w ? 'a window handle' : 'null — blocked, or consumed by the handler'}`);
            break;
          }

          case 'replace':
            this.log.launch(`${tag} → location.replace("${url}")`);
            probe(tag);
            location.replace(url);
            break;

          default:
            this.log.error(`unknown trigger "${method}"`);
        }
      } catch (err) {
        this.log.error(`${tag} threw: ${err && err.message ? err.message : err}`);
      }
    }

    // The customer event, field for field: a bare `google-chrome:` with no `//`, no host, no
    // path. Forced here rather than read off the form, because picking any URL shape other than
    // "scheme only" produces `google-chrome://…` and no longer matches what is being reproduced.
    customerRepro(probe) {
      const url = this.buildUrl({ scheme: 'google-chrome', shape: 'bare' });
      this.log.launch(`customer repro — ${url}`);
      this.log.info('customer repro — expect externalApp.scheme === "google-chrome:" and ' +
        'externalApp.url === "google-chrome:" (no "//", no host); compare policy.action and ' +
        'whether a launchBehavior field is present');
      this.attempt('gesture', url, 0, ' customer', probe);
    }

    // Walk the repro matrix, one case at a time, far enough apart that each case's verdict is
    // unambiguously its own. Every case is a real in-handler navigation.
    repro(cases, { intervalMs = 3000, probe } = {}) {
      cases.forEach((c, i) => {
        setTimeout(() => {
          const scheme = c.unique ? this.uniqueScheme(c.scheme) : c.scheme;
          const target = !c.pathProbe ? c.target
            : c.shape === 'opaque' ? `payload-${this._token()}`
            : `${location.host}/p-${this._token()}?q=${this._token()}`;
          const url = this.buildUrl({ scheme, shape: c.shape, target });
          this.log.launch(`repro ${c.id} — ${url}`);
          this.log.info(`repro ${c.id} — ${c.expect}`);
          this.attempt('gesture', url, 0, ` repro:${c.id}`, probe);
        }, i * intervalMs);
      });
      this.log.info(`repro matrix queued — ${cases.length} cases, ${intervalMs} ms apart; ` +
        'match each console row to a case by its scheme, then compare the Action column');
    }

    // Which app answers a scheme is the OS's call, not the page's — so the only way to find
    // the scheme a given browser/app owns is to try them and watch for a reaction. Walks the
    // candidates one at a time, spaced so each probe window closes before the next attempt.
    scan(candidates, { intervalMs = 2500, probe } = {}) {
      candidates.forEach((cand, i) => {
        setTimeout(() => {
          const url = this.buildUrl({ scheme: cand.scheme, shape: cand.shape || 'bare' });
          this.log.launch(`scan ${i + 1}/${candidates.length} — ${url}${cand.note ? `  (${cand.note})` : ''}`);
          this.attempt('gesture', url, 0, ` scan:${cand.scheme}`, probe);
        }, i * intervalMs);
      });
      this.log.info(`scheme scan queued — ${candidates.length} schemes, ${intervalMs} ms apart; ` +
        'a "handler most likely opened" verdict names the scheme that owns the app');
    }

    // The browser-sniffing redirect: "not Chrome by brands → bounce through google-chrome:".
    chromeRedirect(identity) {
      const names = identity.brandNames();
      this.log.launch(`chrome-redirect pattern — brands = [${names.join(' | ') || 'unavailable'}]`);
      if (identity.isGoogleChrome()) {
        this.log.info('isChrome() === true → redirect branch skipped (brands include "Google Chrome")');
        return null;
      }
      const url = 'google-chrome://' + location.href.replace(/^https?:\/\//i, '');
      this.log.info('isChrome() === false → taking the "open in Chrome" branch');
      this.attempt('gesture', url, 0);
      return url;
    }

    _clickAnchor(url) {
      const a = document.createElement('a');
      a.href = url;
      a.rel = 'noopener';
      a.style.display = 'none';
      document.body.appendChild(a);
      a.click();
      a.remove();
    }

    _iframe(url) {
      const f = document.createElement('iframe');
      f.style.display = 'none';
      f.src = url;
      document.body.appendChild(f);
      setTimeout(() => f.remove(), 2000);
    }
  }

  // ════════════════════════════════════════════════════════════════════════════
  // LaboratoryPage — controller.
  // ════════════════════════════════════════════════════════════════════════════
  class LaboratoryPage {
    constructor(root = document) {
      this.$ = (id) => root.getElementById(id);
      this.activity     = new ActivityLog(this.$('activityLog'));
      this.downloader   = new FileDownloader();
      this.pdfGenerator = new PdfGenerator();
      this.canvasPdf    = new CanvasPdfGenerator();
      this.preview      = new RecordPreview();
      this.canvasDialog = new CanvasDialog({ onConfirm: (meta) => this._performDownload(meta) });
      this.canvasApp = new CanvasApp({
        records: () => this.pdfGenerator.records(
          parseInt(this.$('dlSize')?.value || '200', 10) || 200, 'CI/DI Records'),
        onDownload: () => this._onDownload(),
      });
      this.uploader = new FileUploader({
        dropzone: this.$('dropzone'),
        input:    this.$('fileInput'),
        onAdd:    (added) => this._onFilesAdded(added),
      });
      this.identity    = new BrowserIdentity();
      this.launchProbe = new LaunchProbe(1500);
      this.launcher    = new ExternalAppLauncher({
        log:   this.activity,
        probe: (tag) => {
          if (!this.$('exProbe')?.checked) return;
          this.launchProbe.watch(tag, (verdict) => this.activity.info(verdict));
        },
      });
    }

    init() {
      this.$('dlBtn')?.addEventListener('click', () => this._onDownload());
      this.$('dlPageCanvas')?.addEventListener('change', (e) => {
        if (e.target.checked) this.canvasApp.open(); else this.canvasApp.close();
      });
      this.$('clearLogBtn')?.addEventListener('click', () => {
        this.activity.clear();
        this.activity.info('log cleared');
      });
      this.$('fileList')?.addEventListener('click', (e) => {
        const btn = e.target.closest('[data-download]');
        if (btn) this._onReDownload(btn.getAttribute('data-download'));
      });
      this._initExternalLaunch();
      this.activity.info('Laboratory ready');
    }

    // ── External app launch (custom scheme navigation) ──
    _initExternalLaunch() {
      const ids = ['exScheme', 'exCustom', 'exPath', 'exTarget',
                   'exTrigger', 'exDelay', 'exRepeat', 'exInterval'];
      ids.forEach(id => this.$(id)?.addEventListener('input', () => this._syncLaunchForm()));
      ids.forEach(id => this.$(id)?.addEventListener('change', () => this._syncLaunchForm()));

      this.$('exLaunchBtn')?.addEventListener('click', () => this._onLaunch());
      this.$('exRedirectBtn')?.addEventListener('click', () => this.launcher.chromeRedirect(this.identity));
      this.$('exIdentBtn')?.addEventListener('click', () => this._onShowIdentity());
      this.$('exCustomerBtn')?.addEventListener('click', () => this.launcher.customerRepro(
        (tag) => this.launchProbe.watch(tag, (verdict) => this.activity.info(verdict))));
      this.$('exReproBtn')?.addEventListener('click', () => this.launcher.repro(REPRO_MATRIX, {
        probe: (tag) => this.launchProbe.watch(tag, (verdict) => this.activity.info(verdict)),
      }));
      this.$('exScanBtn')?.addEventListener('click', () => this.launcher.scan(CANDIDATE_SCHEMES, {
        // The scan is the one place probing is not optional — the verdict IS the result.
        probe: (tag) => this.launchProbe.watch(tag, (verdict) => this.activity.info(verdict)),
      }));
      this.$('exOnLoad')?.addEventListener('change', () => this._persistAutoLaunch());
      this.$('exUniq')?.addEventListener('change', () => this._syncLaunchForm());

      this._syncLaunchForm();
      this._restoreAutoLaunch();
    }

    // Enable the free-text fields only for the options that use them, and keep the
    // "Navigation target" preview showing exactly what will be navigated to.
    _syncLaunchForm() {
      const custom = this.$('exCustom');
      const target = this.$('exTarget');
      if (custom) custom.disabled = this.$('exScheme')?.value !== 'custom';
      const shape = this.$('exPath')?.value;
      if (target) target.disabled = !(shape === 'custom' || shape === 'opaque');

      const cfg = this._launchConfig();
      const text = this.$('exUrlText');
      if (text) {
        const base = this.launcher.buildUrl(cfg);
        text.textContent =
          cfg.uniq === 'scheme' ? this.launcher.buildUrl({ ...cfg, scheme: `${cfg.scheme}-<new>` }) :
          cfg.uniq === 'path'   ? this.launcher.withPathToken(base).replace(/lab-\w+$/, 'lab-<new>') :
                                  base;
      }
      if (this.$('exOnLoad')?.checked) this._persistAutoLaunch();
    }

    _launchConfig() {
      const sel = this.$('exScheme')?.value || 'google-chrome';
      return {
        schemeSel: sel,
        scheme:   sel === 'custom' ? (this.$('exCustom')?.value || '') : sel,
        shape:    this.$('exPath')?.value || 'bare',
        target:   this.$('exTarget')?.value || '',
        uniq:     this.$('exUniq')?.value || 'off',
        method:   this.$('exTrigger')?.value || 'gesture',
        delay:    this.$('exDelay')?.value || 0,
        times:    this.$('exRepeat')?.value || 1,
        interval: this.$('exInterval')?.value || 0,
      };
    }

    _onLaunch() {
      const cfg = this._launchConfig();
      const url = this.launcher.buildUrl(cfg);
      this.launcher.launch({ ...cfg, url });
    }

    _onShowIdentity() {
      const host = this.$('exIdent');
      if (!host) return;
      if (host.style.display !== 'none') { host.style.display = 'none'; return; }
      host.textContent = this.identity.lines().join('\n');
      host.style.display = '';
      this.activity.info(`browser identity — brands: [${this.identity.brandNames().join(' | ') || 'unavailable'}]`);
    }

    // "Fire on every page load" — a site script that runs per navigation is what turns one
    // launch into a stream of events minutes apart, so the config survives a reload.
    _persistAutoLaunch() {
      try {
        if (this.$('exOnLoad')?.checked)
          localStorage.setItem(AUTO_LAUNCH_KEY, JSON.stringify(this._launchConfig()));
        else
          localStorage.removeItem(AUTO_LAUNCH_KEY);
      } catch (e) { /* storage disabled — the checkbox then applies to this load only */ }
    }

    _restoreAutoLaunch() {
      let cfg = null;
      try {
        const raw = localStorage.getItem(AUTO_LAUNCH_KEY);
        if (raw) cfg = JSON.parse(raw);
      } catch (e) { cfg = null; }
      if (!cfg) return;

      const box = this.$('exOnLoad');
      if (box) box.checked = true;
      if (this.$('exScheme'))   this.$('exScheme').value = cfg.schemeSel || cfg.scheme || 'google-chrome';
      if (this.$('exCustom') && cfg.schemeSel === 'custom') this.$('exCustom').value = cfg.scheme || '';
      if (this.$('exPath'))     this.$('exPath').value = cfg.shape || 'bare';
      if (this.$('exTarget'))   this.$('exTarget').value = cfg.target || '';
      if (this.$('exUniq'))     this.$('exUniq').value = cfg.uniq || 'off';
      if (this.$('exTrigger'))  this.$('exTrigger').value = cfg.method || 'gesture';
      if (this.$('exDelay'))    this.$('exDelay').value = cfg.delay ?? 0;
      if (this.$('exRepeat'))   this.$('exRepeat').value = String(cfg.times || 1);
      if (this.$('exInterval')) this.$('exInterval').value = cfg.interval ?? 0;
      this._syncLaunchForm();

      // Page-load fire: no user gesture is involved at all, whatever the chosen trigger.
      const url = this.launcher.buildUrl(cfg);
      setTimeout(() => {
        this.activity.launch(`page-load auto fire (no user gesture) — ${url}`);
        this.launcher.launch({ ...cfg, url });
      }, AUTO_LAUNCH_DELAY_MS);
    }

    // ── Download: canvas render → client-side PDF (no dialog) ──
    _onDownload() {
      const name  = (this.$('dlName')?.value || 'pretzel-sample.pdf').trim() || 'pretzel-sample.pdf';
      const count = parseInt(this.$('dlSize')?.value || '200', 10) || 200;
      const meta = { name, count };
      if (this.$('dlCanvasDialog')?.checked) {
        this.activity.info(`canvas dialog opened — ${name} (${count.toLocaleString()} records)`);
        this.canvasDialog.open(meta);          // dialog rendered on <canvas>; confirm → download
      } else {
        this._performDownload(meta);
      }
    }

    _performDownload(meta) {
      try {
        const t0 = performance.now();
        const title = 'CI/DI Records';
        const lines = LAB_RECORDS.length
          ? [title, 'PII included', '', ...LAB_RECORDS]
          : this.pdfGenerator.records(meta.count, title);

        // Optional on-page preview (none | dom | canvas).
        const render = this.$('dlRender')?.value || 'none';
        const host = this.$('dlPreview');
        if (host) {
          if (render === 'canvas')             this.preview.renderCanvas(host, lines);
          else if (render === 'canvas-hidden') this.preview.renderCanvasHidden(host, lines);
          else if (render === 'dom')           this.preview.renderDom(host, lines);
          else                                 this.preview.clear(host);
        }

        const format = this.$('dlFormat')?.value || 'text';
        const blob = format === 'canvas'
          ? this.canvasPdf.generate(lines)          // canvas → JPEG → image PDF (pixels)
          : this.pdfGenerator.build(lines);         // hand-built text PDF (selectable text)
        const kind = format === 'canvas' ? 'canvas image PDF (rasterized)' : 'text PDF';

        const fname = /\.pdf$/i.test(meta.name)
          ? meta.name : meta.name.replace(/\.[^./\\]+$/, '') + '.pdf';

        const via = this.$('dlVia')?.value || 'blob';
        if (via === 'data') this.downloader.saveDataUrl(blob, fname); // data: URL
        else                this.downloader.saveBlob(blob, fname);    // blob: URL
        const bytes = blob.size;

        const ms = (performance.now() - t0).toFixed(1);
        const nrec = LAB_RECORDS.length || meta.count;
        this.activity.down(
          `${fname} — ${Number(nrec).toLocaleString()} records → ${formatBytes(bytes)} ${kind} via ${via} in ${ms} ms`);
      } catch (err) {
        this.activity.error(`download failed: ${err && err.message ? err.message : err}`);
      }
    }

    // ── Upload ──
    _onFilesAdded(added) {
      added.forEach(f => this.activity.up(`${f.name} — ${f.describe()}`));
      this._renderFileList();
    }

    _onReDownload(id) {
      const f = this.uploader.find(id);
      if (!f) return;
      try {
        const bytes = f.download(this.downloader);
        this.activity.down(`${f.name} — ${formatBytes(bytes)} re-downloaded`);
      } catch (err) {
        this.activity.error(`re-download failed: ${err && err.message ? err.message : err}`);
      }
    }

    _renderFileList() {
      const host = this.$('fileList');
      const badge = this.$('uploadCount');
      if (badge) badge.textContent = String(this.uploader.count());
      if (!host) return;

      if (!this.uploader.count()) {
        host.innerHTML = `<div class="lab-empty">No files uploaded yet.</div>`;
        return;
      }

      host.innerHTML = this.uploader.files.map(f => `
        <div class="lab-file">
          <div class="lab-file-icon">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
              <polyline points="14 2 14 8 20 8"/>
            </svg>
          </div>
          <div class="lab-file-meta">
            <div class="lab-file-name" title="${escHtml(f.name)}">${escHtml(f.name)}</div>
            <div class="lab-file-sub">${escHtml(f.describe())}</div>
          </div>
          <div class="lab-file-actions">
            <button class="btn btn-sm" data-download="${f.id}">Download</button>
          </div>
        </div>`).join('');
    }
  }

  // ── Boot ────────────────────────────────────────────────────────────────────
  function boot() { new LaboratoryPage(document).init(); }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', boot);
  else
    boot();
}());
