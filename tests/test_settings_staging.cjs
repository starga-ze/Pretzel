// The staging registry across a Configuration page load.
//
//   node --test tests/test_settings_staging.cjs
//
// No jsdom and no network: the eight tab modules are loaded into a stubbed context on a tab none
// of them renders for, so what runs is exactly the path that decides the Config Review button —
// each module's load() and the staging.refresh() it ends with.
//
// It exists because of a bug that unit tests could not have caught. refresh() asks EVERY provider
// whether it is dirty, and each module calls it when its OWN load finishes — so a module that
// leaves `deployed` and its staged copy out of step across an await is read in that state by the
// others. Two of them did, and reported their whole domain as deleted for as long as the await
// took: the button lit up with a count on arrival and went out again. Nothing is wrong with any
// one module in isolation, which is the point.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const WWW = path.join(__dirname, '..', 'mgmtd', 'www', 'js');
// Script order as settings.html lists it: the shared dialog surfaces, then the staging layer,
// then the tab modules that register providers against it.
const MODULES = ['table-tools',
                 'ui/modal', 'ui/diff', 'ui/device-test',
                 'settings/draft', 'settings/staging', 'settings/review', 'settings/revert',
                 'ai-provider', 'ai-route', 'sites', 'config',
                 'api-keys', 'endpoints', 'api-connectors', 'users'];

// Enough of a running-config for every module to find its own domain populated. The counts are
// what matter: a domain that comes back empty cannot show this failure, because an empty
// `deployed` matches the empty staged copy a module starts with.
const CONFIG = {
  pretzel: {
    site: {
      sites: [{ oid: 's1', name: 'HQ', description: '' }],
      ngfw_devices: [{ oid: 'd1', name: 'FW1', site: 's1', target: '10.0.0.1', fingerprint: '' }],
      sase_devices: [{ oid: 'd2', name: 'TEN', site: 's1', target: 'tsg:1', health: { url: '', body: '' } }],
    },
    connector: {
      api_credentials: [
        { oid: 'k1', name: 'fw-key', device: 'd1', endpoint: '/api/?type=keygen', username: 'admin' },
        { oid: 'k2', name: 'sase-key', device: 'd2', endpoint: '', username: '' },
      ],
      endpoints: [
        { oid: 'e1', name: 'system-info', device_type: 'ngfw', subtype: 'xml', path: '/api/' },
        { oid: 'e2', name: 'egress-ip', device_type: 'sase', subtype: 'rest', path: '/v1/ip' },
      ],
      connectors: [{ oid: 'c1', name: 'fw-collect', object: 'd1', profile: 'k1', endpoints: ['e1'] }],
    },
    user: { list: [{ oid: 'u1', username: 'admin', role: 'admin' }] },
  },
  'pretzel-ai': {
    providers: { list: [{ id: 'openai', models: ['gpt-4o'] }] },
    route: { list: [{ service: 'chat', transport: 'direct', guardrail: 'none', airs: { profile_name: 'p' } }] },
  },
};

function makeStorage(seed) {
  const m = new Map(Object.entries(seed || {}));
  const api = {
    getItem: k => (m.has(String(k)) ? m.get(String(k)) : null),
    setItem: (k, v) => m.set(String(k), String(v)),
    removeItem: k => m.delete(String(k)),
    clear: () => m.clear(),
  };
  return new Proxy(api, {
    ownKeys: () => Array.from(m.keys()),
    getOwnPropertyDescriptor: () => ({ enumerable: true, configurable: true }),
    get: (t, p) => (p in t ? t[p] : (typeof p === 'string' && m.has(p) ? m.get(p) : undefined)),
  });
}

// A DOM that answers everything and does nothing. The modules under test do not render here.
const inert = new Proxy({}, {
  get(_, p) {
    if (p === 'querySelectorAll') return () => [];
    if (p === 'querySelector') return () => null;
    if (p === 'classList') return { add() {}, remove() {}, toggle() {}, contains: () => false };
    if (p === 'dataset' || p === 'style') return {};
    if (p === 'options') return [];
    if (typeof p === 'string') return () => {};
    return undefined;
  },
  set: () => true,
});

async function loadPage({ drafts = {}, tab = 'operation' } = {}) {
  const sessionStorage = makeStorage(drafts);
  const listeners = new Map();
  const document = {
    addEventListener(t, fn) { (listeners.get(t) || listeners.set(t, []).get(t)).push(fn); },
    removeEventListener() {},
    dispatchEvent(ev) { (listeners.get(ev && ev.type) || []).forEach(fn => fn(ev)); return true; },
    getElementById: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    createElement: () => inert,
    body: inert,
    documentElement: inert,
    readyState: 'loading',
  };

  const settings = { scopes: JSON.parse(JSON.stringify(CONFIG)), version: 48 };
  const fetchStub = (url) => {
    const u = String(url).split('?')[0];
    const body = u === '/api/settings' ? settings
      : u === '/api/ai/credentials' ? { providers: {}, sealing_available: true }
      : u === '/api/user/credentials' ? { users: {} }
      : {};
    return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(body) });
  };

  const trace = [];
  const NMS = {
    settingsDefaultTab: 'sites',
    currentUser: 'admin',
    onCommit(fn) { NMS._onCommit = fn; },
    onRevert(fn) { NMS._onRevert = fn; },
    onRefresh(fn) { NMS._onRefresh = fn; },
    setPendingChanges(n) {
      trace.push({ n: n | 0, dirty: NMS.staging ? NMS.staging.dirtyKeys() : [] });
    },
    navBadge: { set() {}, paint() {}, paintAll() {} },
    utils: {
      esc: s => String(s == null ? '' : s).replace(/[&<>"]/g, c =>
        ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])),
      newUuid: () => 'u-' + Math.random().toString(16).slice(2),
      enhanceSelects() {},
      siteScope: { get: () => '', set() {}, publish() {} },
      async fetchJSON(url) { return (await fetchStub(url)).json(); },
      async loadSettings() {
        const d = await this.fetchJSON('/api/settings');
        if (!d) return null;
        NMS.draft.checkBase(d.version);
        return d;
      },
    },
  };

  class Ev { constructor(type) { this.type = type; } }
  const ctx = {
    window: { NMS, addEventListener() {}, removeEventListener() {}, innerHeight: 900 },
    document, sessionStorage, localStorage: makeStorage(),
    location: { search: '?tab=' + tab, pathname: '/settings', href: '' },
    fetch: fetchStub, console, Event: Ev, CustomEvent: Ev,
    JSON, Date, Math, Object, Array, String, Number, Boolean, Set, Map, Int32Array,
    Error, parseInt, parseFloat, isNaN, URL, URLSearchParams, Promise, setTimeout, clearTimeout,
    performance: { now: () => 0 },
    AbortSignal: { timeout: () => undefined },
    crypto: { randomUUID: () => 'rnd' },
    CSS: { escape: s => s },
  };
  ctx.window.fetch = fetchStub;
  ctx.window.location = ctx.location;
  ctx.window.document = document;
  ctx.window.sessionStorage = sessionStorage;
  ctx.globalThis = ctx;
  vm.createContext(ctx);

  for (const name of MODULES) {
    vm.runInContext(readFileSync(path.join(WWW, name + '.js'), 'utf8'), ctx, { filename: name + '.js' });
  }

  document.readyState = 'interactive';
  await Promise.all((listeners.get('DOMContentLoaded') || []).map(fn => fn(new Ev('DOMContentLoaded'))));
  await new Promise(r => setTimeout(r, 200));
  return { trace, NMS };
}

test('a clean page load never claims a candidate', async () => {
  const { trace } = await loadPage();
  assert.ok(trace.length > 0, 'the registry never reported at all');
  const claimed = trace.filter(t => t.n > 0);
  assert.deepEqual(
    claimed, [],
    'the Config Review button was turned on during a load with nothing staged, by: '
    + claimed.map(t => `[${t.dirty.join(', ')}]`).join(' '));
});

// The registry's arrays are built inside the sandbox, whose Array is not this realm's, so
// deepEqual would compare prototypes and not contents. Carry them across as plain data.
const keys = (nms) => Array.from(nms.staging.dirtyKeys(), String);

test('every module ends the load reporting clean', async () => {
  const { NMS } = await loadPage();
  assert.deepEqual(keys(NMS), [], 'a module was still dirty after its load settled');
});

test('a staged draft is reported, and stays reported', async () => {
  // One site renamed. The count must reach 1 and never fall back to 0 while the draft is there.
  const staged = [{ oid: 's1', name: 'HQ renamed', description: '' }];
  const { trace, NMS } = await loadPage({ drafts: { 'pz.draft.sites': JSON.stringify(staged) } });
  assert.ok(trace.some(t => t.n > 0), 'a staged draft was never reported');
  assert.equal(trace[trace.length - 1].n, 1, 'the staged draft stopped being reported');
  assert.deepEqual(keys(NMS), ['sites']);
});
