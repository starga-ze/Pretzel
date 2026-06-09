/* settings.js
 *
 * GET  /api/settings              → { daemons: { <svc>: { <domain>: { <key>: val } } } }
 * POST /api/settings/commit       → { reloading, applied, failed, results[] }
 * GET  /api/settings/reload-status→ { status: "idle"|"reloading"|"complete", elapsed_ms }
 *
 * Tab is determined by URL ?tab=icmp|snmp|lldp|topology
 * bootstrap domain is never shown.
 */

(function () {
  'use strict';

  // ── Tab definitions ──────────────────────────────────────────────────────

  const TABS = {
    general: {
      label:    'General',
      title:    'General',
      subtitle: 'Global service behavior and health-check parameters.',
      daemons:  ['engined'],
      domains:  ['heartbeat'],
    },
    icmp: {
      label:    'ICMP',
      title:    'ICMP Probe',
      subtitle: 'Configure scan ranges, excluded IPs, and probe timing for ICMP ping discovery.',
      daemons:  ['icmpd'],
      domains:  ['probe'],
    },
    snmp: {
      label:    'SNMP',
      title:    'SNMP',
      subtitle: 'SNMP polling intervals and community string settings.',
      daemons:  ['snmpd'],
      domains:  [],
    },
    lldp: {
      label:    'LLDP',
      title:    'LLDP',
      subtitle: 'Link Layer Discovery Protocol neighbor detection settings.',
      daemons:  [],
      domains:  [],
    },
    users: {
      label:    'Users',
      title:    'User Management',
      subtitle: 'Manage login accounts and access permissions.',
      daemons:  [],
      domains:  [],
    },
  };

  // Keys that are free-form strings (rendered as text input, not number)
  // Keys rendered with the list-editor modal instead of a plain text input
  const LIST_KEYS = new Set(['scan_cidr', 'excluded_ips']);
  const STRING_KEYS = new Set(['scan_cidr', 'excluded_ips']);

  const KEY_LABELS = {
    scan_cidr:                 'Scan CIDRs',
    excluded_ips:              'Excluded IPs',
    batch_size:                'Batch Size',
    batch_interval_ms:         'Batch Interval (ms)',
    cycle_interval_ms:         'Cycle Interval (ms)',
    reply_idle_timeout_ms:     'Reply Idle Timeout (ms)',
    reply_max_wait_timeout_ms: 'Max Reply Wait (ms)',
    // Heartbeat (General tab)
    poll_interval_ms:          'Poll Interval (ms)',
    response_timeout_ms:       'Response Timeout (ms)',
  };

  const KEY_PLACEHOLDER = {
    scan_cidr:    'e.g. 192.168.0.0/23',
    excluded_ips: 'e.g. 192.168.0.1',
  };

  const KEY_HINT = {
    scan_cidr:    'Add one CIDR block per entry. Multiple CIDRs are all scanned and deduplicated.',
    excluded_ips: 'IPs listed here are skipped even if they fall within a scan CIDR.',
  };

  const DOMAIN_LABELS = {
    probe: 'Probe',
  };

  // ── State ────────────────────────────────────────────────────────────────

  let currentData      = null;
  let activeTab        = 'icmp';
  let selectedQueueKey = null;

  // staged edits: `${daemon} ${domain} ${key}` → {daemon, domain, key, oldValue, newValue}
  const pending = new Map();

  // ── DOM refs ─────────────────────────────────────────────────────────────

  let container, statusEl, pageTitleEl, pageSubtitleEl;
  let discardBtn, reviewBtn, commitPendingBadge, commitPendingCount;
  let reviewOverlay, reviewCloseBtn, reviewCancelBtn;
  let commitBtn, commitStatus, commitProgressFill;
  let commitQueueList, queueTotalBadge, diffViewer, diffPanelHint;

  // List-editor modal state
  let listEditorCallback = null;

  // ── Helpers ───────────────────────────────────────────────────────────────

  function stageKey(daemon, domain, key) { return `${daemon} ${domain} ${key}`; }
  function fieldId(daemon, domain, key)  { return `f__${daemon}__${domain}__${key}`; }

  async function fetchJSON(url, opts) {
    const r = await fetch(url, Object.assign({ credentials: 'same-origin' }, opts));
    if (r.status === 401) { window.location.href = '/index.html'; return null; }
    return r;
  }

  function setStatus(msg, kind) {
    if (!statusEl) return;
    statusEl.textContent = msg || '';
    statusEl.className = 'settings-status' + (kind ? ` ${kind}` : '');
  }

  function setCommitStatus(msg, kind) {
    if (!commitStatus) return;
    commitStatus.textContent = msg || '';
    commitStatus.className = 'modal-status' + (kind ? ` ${kind}` : '');
  }

  function cssEsc(s) { return String(s).replace(/["\\]/g, '\\$&'); }

  // ── Tab init ─────────────────────────────────────────────────────────────

  function initTab() {
    const params = new URLSearchParams(window.location.search);
    activeTab = params.get('tab') || 'icmp';
    if (!TABS[activeTab]) activeTab = 'icmp';

    // Highlight active sub-item in sidebar
    document.querySelectorAll('.nav-subitem[data-tab]').forEach((a) => {
      a.classList.toggle('active', a.dataset.tab === activeTab);
    });

    const tab = TABS[activeTab];
    if (pageTitleEl)    pageTitleEl.textContent    = tab.title;
    if (pageSubtitleEl) pageSubtitleEl.textContent = tab.subtitle;
  }

  // ── List editor modal ─────────────────────────────────────────────────────

  function openListEditor({ title, hint, placeholder, items, onSave }) {
    const overlay = document.getElementById('listEditorOverlay');
    const titleEl = document.getElementById('listEditorTitle');
    const hintEl  = document.getElementById('listEditorHint');
    const listEl  = document.getElementById('listEditorItems');
    if (!overlay) return;

    titleEl.textContent = title;
    hintEl.textContent  = hint || '';

    function renderItems(arr) {
      listEl.innerHTML = '';
      if (arr.length === 0) {
        listEl.innerHTML = '<div class="le-empty">No entries. Add one below.</div>';
        return;
      }
      arr.forEach((val, idx) => {
        const row = document.createElement('div');
        row.className = 'le-item';
        row.innerHTML =
          `<span class="le-item-idx">${idx + 1}</span>` +
          `<span class="le-item-val">${val}</span>` +
          `<button class="le-item-del" data-idx="${idx}" title="Remove">` +
          `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">` +
          `<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button>`;
        listEl.appendChild(row);
      });
    }

    let current = [...items];
    renderItems(current);

    // Delete handler
    listEl.onclick = (e) => {
      const btn = e.target.closest('.le-item-del');
      if (!btn) return;
      current.splice(Number(btn.dataset.idx), 1);
      renderItems(current);
    };

    // Add handler
    const addInput = document.getElementById('listEditorInput');
    const addBtn   = document.getElementById('listEditorAddBtn');
    if (addInput) {
      addInput.value = '';
      addInput.placeholder = placeholder || '';
    }

    function addItem() {
      const val = (addInput?.value || '').trim();
      if (!val) return;
      if (!current.includes(val)) {
        current.push(val);
        renderItems(current);
      }
      if (addInput) addInput.value = '';
      addInput?.focus();
    }

    addBtn && (addBtn.onclick = addItem);
    addInput && (addInput.onkeydown = (e) => { if (e.key === 'Enter') { e.preventDefault(); addItem(); } });

    listEditorCallback = () => onSave(current.join(','));
    overlay.classList.add('visible');
    addInput?.focus();
  }

  function closeListEditor(save) {
    const overlay = document.getElementById('listEditorOverlay');
    if (!overlay) return;
    if (save && listEditorCallback) listEditorCallback();
    listEditorCallback = null;
    overlay.classList.remove('visible');
  }

  // ── List field widget (replaces plain text input for LIST_KEYS) ───────────

  function buildListField(daemon, domain, key, value) {
    const skey = stageKey(daemon, domain, key);

    const wrap = document.createElement('div');
    wrap.className = 'settings-field settings-field-list';
    wrap.dataset.stageKey = skey;

    const label = document.createElement('span');
    label.className = 'settings-field-label';
    label.textContent = KEY_LABELS[key] || key;

    // Tag-list display
    const tags = document.createElement('div');
    tags.className = 'le-tags-wrap';

    const editBtn = document.createElement('button');
    editBtn.className = 'le-edit-btn';
    editBtn.innerHTML =
      `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round">` +
      `<path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/>` +
      `<path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg> Edit`;

    const originalEl = document.createElement('span');
    originalEl.className = 'settings-field-original';

    // Hidden input to track staged value
    const hidden = document.createElement('input');
    hidden.type = 'hidden';
    hidden.id = fieldId(daemon, domain, key);
    hidden.dataset.daemon   = daemon;
    hidden.dataset.domain   = domain;
    hidden.dataset.key      = key;
    hidden.dataset.original = String(value);
    hidden.value = String(value);

    function getItems(v) {
      return String(v).split(',').map(s => s.trim()).filter(Boolean);
    }

    function refreshTags(v) {
      const items = getItems(v);
      tags.innerHTML = '';
      if (items.length === 0) {
        tags.innerHTML = '<span class="le-tag-empty">None</span>';
      } else {
        items.forEach(item => {
          const t = document.createElement('span');
          t.className = 'le-tag';
          t.textContent = item;
          tags.appendChild(t);
        });
      }
    }

    refreshTags(value);

    // Restore staged value if any
    const staged = pending.get(skey);
    if (staged) {
      hidden.value = staged.newValue;
      refreshTags(staged.newValue);
      applyDirty(wrap, originalEl, true, staged.oldValue);
    }

    editBtn.addEventListener('click', () => {
      openListEditor({
        title:       KEY_LABELS[key] || key,
        hint:        KEY_HINT[key] || '',
        placeholder: KEY_PLACEHOLDER[key] || '',
        items:       getItems(hidden.value),
        onSave(newVal) {
          hidden.value = newVal;
          refreshTags(newVal);
          const orig = hidden.dataset.original;
          if (newVal === orig) {
            pending.delete(skey);
            applyDirty(wrap, originalEl, false, orig);
          } else {
            pending.set(skey, { daemon, domain, key, oldValue: orig, newValue: newVal });
            applyDirty(wrap, originalEl, true, orig);
          }
          refreshCardState(daemon, domain);
          refreshPendingBadge();
        },
      });
    });

    wrap.appendChild(label);
    wrap.appendChild(tags);
    wrap.appendChild(editBtn);
    wrap.appendChild(hidden);
    wrap.appendChild(originalEl);
    return wrap;
  }

  // ── Field rendering ───────────────────────────────────────────────────────

  function buildField(daemon, domain, key, value) {
    const skey = stageKey(daemon, domain, key);
    const isStr = STRING_KEYS.has(key);

    const wrap = document.createElement('div');
    wrap.className = 'settings-field';
    wrap.dataset.stageKey = skey;

    const label = document.createElement('span');
    label.className = 'settings-field-label';
    label.textContent = KEY_LABELS[key] || key;
    label.title = key;

    const inputWrap = document.createElement('div');
    inputWrap.className = 'settings-field-input-wrap';

    const input = document.createElement('input');
    input.type = isStr ? 'text' : 'number';
    input.className = 'settings-field-input' + (isStr ? ' settings-field-input-text' : '');
    input.id = fieldId(daemon, domain, key);
    input.value = value;
    input.dataset.daemon   = daemon;
    input.dataset.domain   = domain;
    input.dataset.key      = key;
    input.dataset.original = String(value);
    input.dataset.isStr    = isStr ? '1' : '0';

    const dot = document.createElement('span');
    dot.className = 'settings-field-dirty-dot';

    inputWrap.appendChild(input);
    inputWrap.appendChild(dot);

    const originalEl = document.createElement('span');
    originalEl.className = 'settings-field-original';

    wrap.appendChild(label);
    wrap.appendChild(inputWrap);
    wrap.appendChild(originalEl);

    input.addEventListener('input', () => onFieldInput(input, wrap, originalEl));

    const staged = pending.get(skey);
    if (staged) {
      input.value = staged.newValue;
      applyDirty(wrap, originalEl, true, staged.oldValue);
    }

    return wrap;
  }

  function onFieldInput(input, wrap, originalEl) {
    const { daemon, domain, key, original } = input.dataset;
    const skey = stageKey(daemon, domain, key);

    if (input.dataset.isStr === '1') {
      const val = input.value;
      if (val === original) {
        pending.delete(skey);
        applyDirty(wrap, originalEl, false, original);
      } else {
        pending.set(skey, { daemon, domain, key, oldValue: original, newValue: val });
        applyDirty(wrap, originalEl, true, original);
      }
    } else {
      const orig = Number(original);
      const num  = Number(input.value);
      if (!Number.isFinite(num)) { applyDirty(wrap, originalEl, true, orig); return; }
      if (num === orig) {
        pending.delete(skey);
        applyDirty(wrap, originalEl, false, orig);
      } else {
        pending.set(skey, { daemon, domain, key, oldValue: orig, newValue: num });
        applyDirty(wrap, originalEl, true, orig);
      }
    }

    refreshCardState(daemon, domain);
    refreshPendingBadge();
  }

  function applyDirty(wrap, originalEl, dirty, originalValue) {
    wrap.classList.toggle('dirty', dirty);
    originalEl.textContent = dirty ? `was: ${originalValue}` : '';
  }

  function refreshCardState(daemon, domain) {
    const card = container?.querySelector(
      `.settings-domain-card[data-daemon="${cssEsc(daemon)}"][data-domain="${cssEsc(domain)}"]`);
    if (!card) return;
    card.classList.toggle('has-changes',
      Array.from(pending.values()).some(p => p.daemon === daemon && p.domain === domain));
  }

  // ── Layout ────────────────────────────────────────────────────────────────

  function buildSectionHeader(text) {
    const h = document.createElement('div');
    h.className = 'settings-section-header';
    h.textContent = text;
    return h;
  }

  const PROBE_TARGET_KEYS = new Set(['scan_cidr', 'excluded_ips']);
  const PROBE_TIMING_KEYS = new Set([
    'batch_size', 'batch_interval_ms', 'cycle_interval_ms',
    'reply_idle_timeout_ms', 'reply_max_wait_timeout_ms',
  ]);

  function buildDomainCard(daemon, domain, values) {
    const card = document.createElement('div');
    card.className = 'settings-domain-card';
    card.dataset.daemon = daemon;
    card.dataset.domain = domain;

    const keys = Object.keys(values);

    if (domain === 'probe') {
      const targetKeys = keys.filter(k => PROBE_TARGET_KEYS.has(k));
      const timingKeys = keys.filter(k => PROBE_TIMING_KEYS.has(k));

      if (targetKeys.length) {
        card.appendChild(buildSectionHeader('Scan Targets'));
        const g = document.createElement('div');
        g.className = 'settings-field-grid settings-field-grid-list';
        targetKeys.forEach(k => g.appendChild(
          LIST_KEYS.has(k) ? buildListField(daemon, domain, k, values[k]) : buildField(daemon, domain, k, values[k])
        ));
        card.appendChild(g);
      }
      if (timingKeys.length) {
        card.appendChild(buildSectionHeader('Timing'));
        const g = document.createElement('div');
        g.className = 'settings-field-grid';
        timingKeys.forEach(k => g.appendChild(buildField(daemon, domain, k, values[k])));
        card.appendChild(g);
      }
    } else {
      const g = document.createElement('div');
      g.className = 'settings-field-grid';
      keys.forEach(k => g.appendChild(buildField(daemon, domain, k, values[k])));
      card.appendChild(g);
    }

    card.classList.toggle('has-changes',
      Array.from(pending.values()).some(p => p.daemon === daemon && p.domain === domain));

    return card;
  }

  function renderTabContent(data) {
    container.innerHTML = '';

    const tab = TABS[activeTab];
    if (!tab) return;

    const daemons = (data && data.daemons) || {};
    let anyField = false;

    tab.daemons.forEach((daemon) => {
      const tuning = daemons[daemon] || {};
      tab.domains.forEach((domain) => {
        const values = tuning[domain];
        if (!values || typeof values !== 'object' || Object.keys(values).length === 0) return;
        container.appendChild(buildDomainCard(daemon, domain, values));
        anyField = true;
      });
    });

    if (!anyField) {
      container.innerHTML = '<div class="settings-empty">No configurable parameters for this section.</div>';
    }
  }

  function render(data) {
    currentData = data;
    renderTabContent(data);
  }

  // ── Pending badge ─────────────────────────────────────────────────────────

  function refreshPendingBadge() {
    const count = pending.size;
    commitPendingCount.textContent = String(count);
    commitPendingBadge.classList.toggle('visible', count > 0);
    reviewBtn.disabled = count === 0;
  }

  function discardAll() {
    if (pending.size === 0) return;
    pending.clear();
    if (currentData) renderTabContent(currentData);
    refreshPendingBadge();
    setStatus('All changes discarded.');
  }

  // ── Pending grouping ──────────────────────────────────────────────────────

  function groupPending() {
    const map = new Map();
    pending.forEach((c) => {
      const k = `${c.daemon}::${c.domain}`;
      if (!map.has(k)) map.set(k, { daemon: c.daemon, domain: c.domain, changes: [] });
      map.get(k).changes.push(c);
    });
    return [...map.values()];
  }

  // ── JSON diff ─────────────────────────────────────────────────────────────

  function renderDiff(daemon, domain, changes) {
    diffViewer.innerHTML = '';
    if (diffPanelHint) diffPanelHint.textContent = `${daemon} · ${DOMAIN_LABELS[domain] || domain}`;

    const before = Object.assign({}, currentData?.daemons?.[daemon]?.[domain] || {});
    const after  = Object.assign({}, before);
    changes.forEach(c => { before[c.key] = c.oldValue; after[c.key] = c.newValue; });
    const changed = new Set(changes.map(c => c.key));

    const block = document.createElement('div');
    block.className = 'diff-block';
    const hunk = document.createElement('div');
    hunk.className = 'diff-hunk';
    hunk.textContent = `@@ ${daemon}.${domain} @@`;
    block.appendChild(hunk);

    Object.keys(before).forEach((key, i, arr) => {
      const comma = i < arr.length - 1 ? ',' : '';
      if (changed.has(key)) {
        [['removed', before[key]], ['added', after[key]]].forEach(([type, val]) => {
          const row = document.createElement('div');
          row.className = `diff-line diff-line-${type}`;
          row.textContent = ` "${key}": ${JSON.stringify(val)}${comma}`;
          block.appendChild(row);
        });
      } else {
        const row = document.createElement('div');
        row.className = 'diff-line diff-line-ctx';
        row.textContent = ` "${key}": ${JSON.stringify(before[key])}${comma}`;
        block.appendChild(row);
      }
    });

    diffViewer.appendChild(block);
  }

  // ── Commit queue ──────────────────────────────────────────────────────────

  function iconSVG(type) {
    const icons = {
      pending: `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>`,
      running: `<svg class="spin" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>`,
      success: `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="20 6 9 17 4 12"/></svg>`,
      error:   `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>`,
    };
    return icons[type] || '';
  }

  let commitQueue = [];

  function buildCommitQueue() {
    commitQueue = groupPending().map(g => Object.assign(g, { status: 'pending' }));
  }

  function renderCommitQueueList() {
    commitQueueList.innerHTML = '';
    if (queueTotalBadge) queueTotalBadge.textContent = String(commitQueue.length);

    if (commitQueue.length === 0) {
      commitQueueList.innerHTML = '<div class="loading-row" style="padding:16px">No changes staged.</div>';
      return;
    }

    commitQueue.forEach((item, idx) => {
      const el = document.createElement('div');
      el.className = 'queue-item' + (selectedQueueKey === `${item.daemon}/${item.domain}` ? ' selected' : '');
      el.dataset.queueIdx = idx;

      const iconWrap = document.createElement('div');
      iconWrap.className = `queue-item-icon status-${item.status}`;
      iconWrap.innerHTML = iconSVG(item.status);

      const body = document.createElement('div');
      body.className = 'queue-item-body';
      body.innerHTML = `<div class="queue-item-name">${item.daemon}</div>
                        <div class="queue-item-sub">${DOMAIN_LABELS[item.domain] || item.domain}</div>`;

      const cnt = document.createElement('span');
      cnt.className = 'queue-item-count';
      cnt.textContent = item.changes.length;

      el.appendChild(iconWrap);
      el.appendChild(body);
      el.appendChild(cnt);
      el.addEventListener('click', () => {
        selectedQueueKey = `${item.daemon}/${item.domain}`;
        renderCommitQueueList();
        renderDiff(item.daemon, item.domain, item.changes);
      });

      commitQueueList.appendChild(el);
    });
  }

  function updateQueueItemStatus(idx, status) {
    commitQueue[idx].status = status;
    const el = commitQueueList.querySelector(`[data-queue-idx="${idx}"]`);
    if (!el) return;
    const iw = el.querySelector('.queue-item-icon');
    if (iw) { iw.className = `queue-item-icon status-${status}`; iw.innerHTML = iconSVG(status); }
  }

  // ── Modal ─────────────────────────────────────────────────────────────────

  function openReviewModal() {
    if (pending.size === 0) return;
    buildCommitQueue();
    selectedQueueKey = null;
    diffViewer.innerHTML = `<div class="diff-placeholder">
      <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" opacity=".3">
        <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
        <polyline points="14 2 14 8 20 8"/>
      </svg>
      <span>Select a queue item to preview the JSON diff</span>
    </div>`;
    if (diffPanelHint) diffPanelHint.textContent = 'Select a queue item';
    renderCommitQueueList();
    if (commitProgressFill) commitProgressFill.style.width = '0%';
    setCommitStatus('');
    commitBtn.disabled = false;
    reviewOverlay.classList.add('visible');
  }

  function closeReviewModal() {
    reviewOverlay.classList.remove('visible');
  }

  // ── Commit ────────────────────────────────────────────────────────────────

  const RELOAD_POLL_MS    = 500;
  const RELOAD_TIMEOUT_MS = 30000;

  function setProgress(pct) {
    if (commitProgressFill) commitProgressFill.style.width = `${pct}%`;
  }

  function commitFailed(msg) {
    commitQueue.forEach((_, i) => updateQueueItemStatus(i, 'error'));
    setCommitStatus(msg, 'error');
    setProgress(0);
    commitBtn.disabled  = false;
    discardBtn.disabled = false;
    reviewBtn.disabled  = pending.size === 0;
  }

  async function commitChanges() {
    if (commitQueue.length === 0) return;
    commitBtn.disabled  = true;
    discardBtn.disabled = true;
    reviewBtn.disabled  = true;
    commitQueue.forEach((_, i) => updateQueueItemStatus(i, 'running'));
    setProgress(0);
    setCommitStatus('Saving settings…');

    const changes = commitQueue.map(item => {
      const values = {};
      item.changes.forEach(c => { values[c.key] = c.newValue; });
      return { daemon: item.daemon, domain: item.domain, values };
    });

    let resp = null;
    try {
      const r = await fetchJSON('/api/settings/commit', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ changes }),
      });
      if (!r) return;
      resp = await r.json().catch(() => ({}));
      if (!r.ok && !resp.results) { commitFailed(`Commit failed: ${resp.error || r.status}`); return; }
    } catch (e) {
      commitFailed(`Commit failed: ${e}`);
      return;
    }

    const results = resp.results || [];
    let applied = 0;

    commitQueue.forEach((item, i) => {
      const res = results.find(r => r.daemon === item.daemon && r.domain === item.domain);
      if (res && res.status === 'ok') {
        item.changes.forEach(c => {
          pending.delete(stageKey(c.daemon, c.domain, c.key));
          if (currentData?.daemons?.[item.daemon]?.[item.domain])
            currentData.daemons[item.daemon][item.domain][c.key] = c.newValue;
        });
        applied++;
      } else if (res) {
        updateQueueItemStatus(i, 'error');
      }
    });

    if (applied === 0 && (resp.failed || 0) > 0) {
      commitFailed('All changes failed to apply.');
      refreshPendingBadge();
      return;
    }

    setProgress(25);

    if (!resp.reloading) {
      commitQueue.forEach((_, i) => updateQueueItemStatus(i, 'success'));
      setProgress(100);
      setCommitStatus(`${applied} change${applied !== 1 ? 's' : ''} applied.`, 'success');
      discardBtn.disabled = false;
      reviewBtn.disabled  = pending.size === 0;
      refreshPendingBadge();
      if (currentData) renderTabContent(currentData);
      setTimeout(closeReviewModal, 1400);
      return;
    }

    setCommitStatus('Restarting services…');
    const pollStart = Date.now();

    await new Promise((resolve) => {
      const timer = setInterval(async () => {
        const elapsed = Date.now() - pollStart;
        setProgress(25 + 65 * Math.min(elapsed / RELOAD_TIMEOUT_MS, 1));

        if (elapsed >= RELOAD_TIMEOUT_MS) {
          clearInterval(timer);
          commitFailed('Timeout — check daemon logs for details.');
          resolve();
          return;
        }

        try {
          const r = await fetchJSON('/api/settings/reload-status');
          if (!r) { clearInterval(timer); resolve(); return; }
          const s = await r.json().catch(() => ({}));
          if (s.status === 'complete') { clearInterval(timer); resolve(); }
        } catch (_) { /* keep polling */ }
      }, RELOAD_POLL_MS);
    });

    if (Date.now() - pollStart >= RELOAD_TIMEOUT_MS) return;

    commitQueue.forEach((item, i) => {
      const res = results.find(r => r.daemon === item.daemon && r.domain === item.domain);
      updateQueueItemStatus(i, (!res || res.status === 'ok') ? 'success' : 'error');
    });

    setProgress(100);
    setCommitStatus(`${applied} change${applied !== 1 ? 's' : ''} deployed — services restarted.`, 'success');
    setStatus(`${applied} change${applied !== 1 ? 's' : ''} deployed.`, 'success');
    discardBtn.disabled = false;
    reviewBtn.disabled  = pending.size === 0;
    refreshPendingBadge();
    if (currentData) renderTabContent(currentData);
    setTimeout(closeReviewModal, 1200);
  }

  // ── Load ──────────────────────────────────────────────────────────────────

  async function load() {
    setStatus('Loading…');
    try {
      const r = await fetchJSON('/api/settings');
      if (!r) return;
      if (!r.ok) { setStatus(`Failed to load settings (${r.status})`, 'error'); return; }
      const data = await r.json();
      pending.clear();
      render(data);
      refreshPendingBadge();
      setStatus('');
    } catch (e) {
      setStatus(`Failed to load settings: ${e}`, 'error');
    }
  }

  // ── Init ──────────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', () => {
    container          = document.getElementById('settingsGroups');
    statusEl           = document.getElementById('settingsStatus');
    pageTitleEl        = document.getElementById('pageTitle');
    pageSubtitleEl     = document.getElementById('pageSubtitle');

    commitPendingBadge = document.getElementById('commitPendingBadge');
    commitPendingCount = document.getElementById('commitPendingCount');
    discardBtn         = document.getElementById('discardBtn');
    reviewBtn          = document.getElementById('reviewBtn');

    reviewOverlay      = document.getElementById('reviewOverlay');
    reviewCloseBtn     = document.getElementById('reviewCloseBtn');
    reviewCancelBtn    = document.getElementById('reviewCancelBtn');
    commitBtn          = document.getElementById('commitBtn');
    commitStatus       = document.getElementById('commitStatus');
    commitProgressFill = document.getElementById('commitProgressFill');
    commitQueueList    = document.getElementById('commitQueueList');
    queueTotalBadge    = document.getElementById('queueTotalBadge');
    diffViewer         = document.getElementById('diffViewer');
    diffPanelHint      = document.getElementById('diffPanelHint');

    if (!container) return;

    initTab();

    // List editor modal
    document.getElementById('listEditorSaveBtn')?.addEventListener('click', () => closeListEditor(true));
    document.getElementById('listEditorCancelBtn')?.addEventListener('click', () => closeListEditor(false));
    document.getElementById('listEditorCloseBtn')?.addEventListener('click', () => closeListEditor(false));
    document.getElementById('listEditorOverlay')?.addEventListener('click', e => {
      if (e.target === document.getElementById('listEditorOverlay')) closeListEditor(false);
    });

    discardBtn?.addEventListener('click', discardAll);
    reviewBtn?.addEventListener('click', openReviewModal);
    reviewCancelBtn?.addEventListener('click', closeReviewModal);
    reviewCloseBtn?.addEventListener('click', closeReviewModal);
    reviewOverlay?.addEventListener('click', e => { if (e.target === reviewOverlay) closeReviewModal(); });
    commitBtn?.addEventListener('click', commitChanges);
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && reviewOverlay?.classList.contains('visible')) closeReviewModal();
    });

    load();
  });
})();
