/* settings.js — daemon tuning dashboard (GET/POST /api/settings) */

(function () {
  'use strict';

  const DAEMON_LABELS = {
    engined:   'Engine (engined)',
    authd:     'Auth (authd)',
    icmpd:     'ICMP Probe (icmpd)',
    mgmtd:     'Management (mgmtd)',
    snmpd:     'SNMP (snmpd)',
    topologyd: 'Topology (topologyd)',
    ipcd:      'IPC Broker (ipcd)',
  };

  const DOMAIN_LABELS = {
    bootstrap:   'Bootstrap',
    heartbeat:   'Heartbeat',
    probe:       'ICMP Probe',
    ipc_connect: 'IPC Connect Retry',
  };

  const KEY_LABELS = {
    client_hello_interval_ms:  'Client hello interval (ms)',
    runtime_ready_interval_ms: 'Runtime-ready interval (ms)',
    sync_request_interval_ms:  'Sync request interval (ms)',
    bootstrap_timeout_ms:      'Bootstrap timeout (ms)',
    poll_interval_ms:          'Poll interval (ms)',
    response_timeout_ms:       'Response timeout (ms)',
    batch_size:                'Probe batch size',
    batch_interval_ms:         'Batch interval (ms)',
    cycle_interval_ms:         'Cycle interval (ms)',
    reply_idle_timeout_ms:     'Reply idle timeout (ms)',
    reply_max_wait_timeout_ms: 'Reply max-wait timeout (ms)',
    max_attempts:              'Max connect attempts',
    retry_delay_ms:            'Retry delay (ms)',
  };

  let container = null;
  let statusEl = null;

  async function fetchJSON(url, options) {
    const r = await fetch(url, Object.assign({ credentials: 'same-origin' }, options));
    if (r.status === 401) { window.location.href = '/index.html'; return null; }
    return r;
  }

  function setStatus(msg, isError) {
    if (!statusEl) return;
    statusEl.textContent = msg || '';
    statusEl.style.color = isError ? 'var(--danger, #e5484d)' : '';
  }

  function fieldId(daemon, domain, key) {
    return `tuning__${daemon}__${domain}__${key}`;
  }

  function buildDomainCard(daemon, domain, values) {
    const card = document.createElement('div');
    card.className = 'card';
    card.style.marginBottom = '12px';

    const title = document.createElement('div');
    title.className = 'card-label';
    title.textContent = DOMAIN_LABELS[domain] || domain;
    card.appendChild(title);

    const grid = document.createElement('div');
    grid.style.display = 'grid';
    grid.style.gridTemplateColumns = 'repeat(auto-fill, minmax(220px, 1fr))';
    grid.style.gap = '10px';
    grid.style.margin = '10px 0';

    const keys = Object.keys(values);
    keys.forEach((key) => {
      const wrap = document.createElement('label');
      wrap.style.display = 'flex';
      wrap.style.flexDirection = 'column';
      wrap.style.fontSize = '12px';
      wrap.style.gap = '4px';

      const span = document.createElement('span');
      span.textContent = KEY_LABELS[key] || key;
      span.style.color = 'var(--text-muted, #888)';

      const input = document.createElement('input');
      input.type = 'number';
      input.id = fieldId(daemon, domain, key);
      input.value = values[key];
      input.className = 'settings-input';
      input.style.padding = '6px 8px';
      input.style.borderRadius = '6px';
      input.style.border = '1px solid var(--border, #333)';
      input.style.background = 'var(--bg-elevated, transparent)';
      input.style.color = 'inherit';

      wrap.appendChild(span);
      wrap.appendChild(input);
      grid.appendChild(wrap);
    });

    card.appendChild(grid);

    const saveBtn = document.createElement('button');
    saveBtn.className = 'btn btn-primary';
    saveBtn.textContent = 'Save & Apply';
    saveBtn.addEventListener('click', () => saveDomain(daemon, domain, keys, saveBtn));
    card.appendChild(saveBtn);

    return card;
  }

  async function saveDomain(daemon, domain, keys, btn) {
    const values = {};
    for (const key of keys) {
      const el = document.getElementById(fieldId(daemon, domain, key));
      if (!el) continue;
      const num = Number(el.value);
      if (!Number.isFinite(num)) {
        setStatus(`${daemon}.${domain}.${key}: invalid number`, true);
        return;
      }
      values[key] = num;
    }

    btn.disabled = true;
    setStatus(`Saving ${daemon}.${domain}…`);

    try {
      const r = await fetchJSON('/api/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ daemon, domain, values }),
      });
      if (!r) return;

      if (!r.ok) {
        const body = await r.json().catch(() => ({}));
        setStatus(`Failed to save ${daemon}.${domain}: ${body.error || r.status}`, true);
        return;
      }

      setStatus(`Saved ${daemon}.${domain} — applied without restart`);
    } catch (e) {
      setStatus(`Failed to save ${daemon}.${domain}: ${e}`, true);
    } finally {
      btn.disabled = false;
    }
  }

  function render(data) {
    container.innerHTML = '';

    const daemons = (data && data.daemons) || {};
    const names = Object.keys(daemons);

    if (names.length === 0) {
      container.innerHTML = '<div class="loading-row">No tunable settings found.</div>';
      return;
    }

    names.forEach((daemon) => {
      const tuning = daemons[daemon] || {};
      const domains = Object.keys(tuning);
      if (domains.length === 0) return;

      const section = document.createElement('div');
      section.style.marginBottom = '24px';

      const heading = document.createElement('div');
      heading.style.fontWeight = '600';
      heading.style.fontSize = '14px';
      heading.style.margin = '0 0 10px';
      heading.textContent = DAEMON_LABELS[daemon] || daemon;
      section.appendChild(heading);

      domains.forEach((domain) => {
        const values = tuning[domain];
        if (!values || typeof values !== 'object') return;
        section.appendChild(buildDomainCard(daemon, domain, values));
      });

      container.appendChild(section);
    });
  }

  async function load() {
    setStatus('Loading…');
    try {
      const r = await fetchJSON('/api/settings');
      if (!r) return;
      if (!r.ok) {
        setStatus(`Failed to load settings (${r.status})`, true);
        return;
      }
      const data = await r.json();
      render(data);
      setStatus('');
    } catch (e) {
      setStatus(`Failed to load settings: ${e}`, true);
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    container = document.getElementById('settingsGroups');
    statusEl = document.getElementById('settingsStatus');
    if (!container) return;

    load();
  });
})();
