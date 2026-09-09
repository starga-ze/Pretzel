/* topology.js — Insight › Site Infrastructure
 *
 * What this page is for
 * ---------------------
 * Prisma Access is not a box you can point at: it is a fabric that scales out and in per region,
 * and the only way an operator or an admin can judge it is to SEE it. So this page draws the
 * estate the way Palo Alto draws it in its own architecture picture — who connects (Mobile Users,
 * Remote Networks), what they connect into (per-region load balancers and gateway nodes), and
 * where the traffic leaves for (Internet/SaaS, private apps behind a Service Connection).
 *
 * Where the drawing comes from
 * ----------------------------
 * Today, one source: GET /api/topology/sase — the last getPrismaAccessIP document each SASE tenant
 * answered with (sase_device.egress_result). That document is an inventory of addresses, and the
 * topology is inferred from two of its fields:
 *
 *   serviceType  gp_gateway  → GlobalProtect gateway on an MU-SPN
 *                swg_proxy   → Explicit Proxy (SWG) on an MU-SPN
 *                gp_portal   → portal: agent config and the gateway list (control plane, not traffic)
 *   addressType  active                → a service node
 *                network_load_balancer → the NLB in front of those nodes
 *                auth_cache_service    → a shared global service, not a per-region node
 *
 * Palo Alto's own vocabulary, used verbatim in the drawing so it matches their diagrams and docs:
 *
 *   MU-SPN   Mobile User Security Processing Node — where mobile-user traffic terminates. The SAME
 *            node type serves GlobalProtect AND Explicit Proxy; the API reports them as two
 *            serviceTypes with their own addresses, not as two kinds of node.
 *   RN-SPN   Remote Network SPN — where branch IPsec tunnels terminate.
 *   SC-CAN   Service Connection Corporate Access Node — the path to the customer's data centre.
 *            An SC-CAN performs NO inspection, which is why the on-premise NGFW at the far end is
 *            drawn as part of this picture: that is where private-app policy is actually enforced.
 *
 * Three kinds of endpoint reach the fabric, and they do NOT map one-to-one onto one node each:
 *
 *   GlobalProtect app        L4 tunnel to the GP Gateway on an MU-SPN
 *   Prisma Access Browser    L7 proxy session to the Explicit Proxy (SWG)
 *   Browser + PAC            same SWG, reached by PAC rules instead of the managed browser
 *
 * The two are chained, not exclusive: with GlobalProtect up in full tunnel, a Prisma Access Browser
 * (or PAC'd browser) session still goes into the L4 tunnel first and is proxied by the SWG behind
 * it — GP Gateway → Explicit Proxy → internet, both hops inside Prisma Access. Only under split
 * tunnel / proxy mode does the proxy session leave the endpoint directly. The drawing carries both:
 * a direct lane from the browser rows, and a chained hop from the gateway group to the proxy group.
 *
 * Prisma Access is Mobile Users + Remote Networks + Service Connections. This API sees only the
 * first, and the SCM deployment read adds the Service Connections. RN is therefore drawn as a real
 * lane in the picture but marked "API pending" — the
 * shape is already correct, so when the IPsec / routing / ZTNA-connector reads land they fill lanes
 * that already exist rather than forcing the page to be redrawn.
 *
 * Honesty about "live"
 * --------------------
 * The moving dots say a path EXISTS and the fabric answered — they are not measured throughput.
 * There is no flow telemetry yet; when there is, dot density is where it goes. Scale in/out, by
 * contrast, is real: node sets are diffed against the previous poll, so a gateway that appeared
 * announces itself and one that vanished leaves a marked slot behind for a cycle.
 */
(function () {
  'use strict';

  // Live re-runs exactly what the topbar's refresh button runs (NMS.onRefresh → load), on a timer.
  // One minute matches the tenant probe's own cycle, so a faster poll could only ever redraw the
  // same answer; the interval is stated on the button rather than left to be guessed at.
  const REFRESH_MS = 60000;
  const REFRESH_LABEL = '1m';

  // Composition is a daemon round trip, not a device call — it settles in tens of milliseconds. The
  // retry is therefore fast and the ceiling low: if topologyd has not answered in a couple of
  // seconds it is not busy, it is not answering, and the page should say that instead of spinning.
  const PENDING_RETRY_MS = 400;

  // How long the composing ring is shown once it appears. See finishComposing.
  const MIN_COMPOSE_MS = 2000;

  // How long to keep asking before calling it a failure. Must exceed MIN_COMPOSE_MS — giving up
  // while the ring is still filling would abandon a composition that is merely slow.
  const COMPOSE_TIMEOUT_MS = 15000;
  const PX_PER_SEC = 92;         // packet speed, so a long path is not also a slow one
  const NS = 'http://www.w3.org/2000/svg';

  const esc = (s) => window.NMS.utils.esc(s);

  // ── Vocabulary ──────────────────────────────────────────────────────────────
  // serviceType → how it is drawn. `flow` decides which lane an inbound link belongs to.
  const SVC = {
    gp_gateway: { label: 'MU-SPN · GlobalProtect', sub: 'GlobalProtect Gateway', tone: 'gw', flow: 'mu',
                  note: '', egress: true },
    swg_proxy:  { label: 'MU-SPN · Explicit Proxy', sub: 'Secure Web Gateway', tone: 'swg', flow: 'swg',
                  note: '', egress: true },
    gp_portal:  { label: 'GP Portal', sub: 'agent config + gateway list', tone: 'portal', flow: 'ctl',
                  note: 'control plane', egress: false },
    // Remote networks are onboarded to a Prisma Access location like any other service, and this same
    // API reports them — service_ip is where the branch's IPsec tunnel lands, plus the egress IPs.
    remote_network: { label: 'RN-SPN · Remote Network', sub: 'branch IPsec termination', tone: 'rn',
                      flow: 'rn', note: '', egress: true },
  };
  const svcOf = (k) => SVC[k] || { label: k || 'unknown service', sub: '', tone: 'other', flow: 'mu',
                                   note: 'unrecognised serviceType', egress: false };

  // The three ways user traffic can land in a traffic region. Which of them a tenant has bought is
  // one of the questions this picture gets asked, and a region that answers with only one of the
  // three used to be drawn as though the other two were not part of the vocabulary. They are always
  // drawn now — an absent one as the empty slot it is, in the same hatched "not configured"
  // treatment the endpoint cards already use, so "no proxy in Hong Kong" reads as a fact about the
  // tenant rather than as a region that happens to look shorter than its neighbour.
  const BASELINE_SVC = ['gp_gateway', 'swg_proxy', 'remote_network'];

  // `active` in this API means "an in-service egress address for this location" — it does not say
  // which node is currently carrying a session, and nothing in the payload does. Per-node liveness
  // would have to come from another source, so the label must not imply we know it.
  const ADDR_LABEL = {
    active: 'egress address',
    network_load_balancer: 'load balancer',
    auth_cache_service: 'auth cache',
    service_ip: 'IPsec service IP',
    pre_allocated: 'pre-allocated',
  };

  const ICONS = {
    mobile: '<path d="M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><line x1="9" y1="17" x2="15" y2="17"/>',
    branch: '<path d="M3 21h18"/><path d="M5 21V7l7-4 7 4v14"/><path d="M10 21v-5h4v5"/>',
    cloud:  '<path d="M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"/>',
    globe:  '<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a15 15 0 0 1 0 18a15 15 0 0 1 0-18z"/>',
    shield: '<path d="M12 3l7.5 3v5.5c0 4.6-3.1 8.3-7.5 9.5-4.4-1.2-7.5-4.9-7.5-9.5V6z"/>',
    server: '<rect x="3" y="4" width="18" height="7" rx="2"/><rect x="3" y="13" width="18" height="7" rx="2"/><line x1="7" y1="7.5" x2="7.01" y2="7.5"/><line x1="7" y1="16.5" x2="7.01" y2="16.5"/>',
    // The four Prisma Access cards. Each says what the card IS rather than decorating it: the control
    // plane is the knobs and not the traffic; the data plane is the layers traffic moves through; a
    // Service Connection is a link built into your network; a ZTNA connector is a thing that plugs in
    // from the inside and dials out.
    sliders: '<line x1="3.5" y1="8" x2="20.5" y2="8"/><circle cx="9" cy="8" r="2.3"/>' +
             '<line x1="3.5" y1="16" x2="20.5" y2="16"/><circle cx="15" cy="16" r="2.3"/>',
    layers:  '<path d="M12 3 3 7.5l9 4.5 9-4.5L12 3z"/><path d="M3 13 12 17.5 21 13"/>',
    link:    '<path d="M10.5 13.2a5 5 0 0 0 7.4.4l2.4-2.4a5 5 0 0 0-7-7l-1.4 1.3"/>' +
             '<path d="M13.5 10.8a5 5 0 0 0-7.4-.4l-2.4 2.4a5 5 0 0 0 7 7l1.4-1.3"/>',
    plug:    '<path d="M9 2.5v5M15 2.5v5"/><path d="M6 7.5h12v3.2a6 6 0 0 1-12 0V7.5z"/><path d="M12 17v4.5"/>',
    // A page being read: what the drawer holds is the list behind whatever the card summarises.
    detail: '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h5"/><path d="M8 8h6M8 12h4"/>' +
            '<circle cx="16.5" cy="15.5" r="3.2"/><path d="m19 18 2.2 2.2"/>',
  };

  // Every card opens its detail the same way: this button, in the same corner, and nothing else.
  // A card that is itself clickable teaches nothing about which parts of a drawing are clickable —
  // the reader has to try. One mark, repeated, is a rule the eye learns once.
  const detailBtn = (kind, key, label) =>
    `<button class="topo-det" type="button" data-det="${esc(kind)}" data-detk="${esc(key == null ? '' : key)}"
             title="${esc(label || 'Detail')}" aria-label="${esc(label || 'Detail')}">
       <svg viewBox="0 0 24 24">${ICONS.detail}</svg>
     </button>`;

  const state = {
    site: '',           // '' = Overview; otherwise the scope being drawn
    booting: true,      // first load after a restored scope — draws nothing rather than a wrong empty
    tenants: [],
    ngfw: [],           // on-premise firewalls — the far end of a Service Connection
    region: 'all',      // kept for the per-region dimming; no longer exposed as a control
    live: true,
    flow: true,
    pending: false,     // topologyd has been asked for this site but has not answered yet
    composing: false,   // the ring is on screen and owns the deck until it finishes
    answered: false,    // the composition arrived; the ring may complete
    siteList: [],       // sites as topologyd knows them — the selector no longer infers them
    sources: {},        // per-source counts, so the decks can say WHICH input is missing
    planeOpen: true,    // the data-plane frame — collapsed, the regions fold into one summary row
    view: 'fabric',     // which deck is on screen: 'fabric' | 'ngfw'
    links: [],          // NGFW → fabric/peer edges, composed by topologyd
    shape: {},          // which end is the hub — {kind, tenants, ...}, also from topologyd
    fwOpen: {},         // firewall oid → its port strip is expanded
    ngfwDense: null,    // null = decide from the device count; true/false = the operator decided
    layers: {},         // device oid → { reachable, credential, api } from /api/status/devices
    selected: null,     // node key currently open in the drawer
    generatedAt: '',
    error: '',
  };

  try { state.planeOpen = localStorage.getItem('topo.plane') !== 'closed'; } catch (_) { /* private mode */ }

  // Come back where you left off. Restoring is NOT a scope change: no ring, because the operator did
  // not ask for anything — they returned to a page they were already on, and a two-second ceremony
  // for that reads as the app being slow rather than as it working.
  state.site = window.NMS.utils.siteScope.get();

  // Everything on the page is scoped by site: the SASE tenant that serves it, and the firewalls that
  // sit in it. A site with no SASE tenant is a legitimate answer — the fabric deck says so.
  function siteList() {
    // topologyd sends the declared sites; devices contribute any the config did not list. A site
    // with nothing in it stays selectable — an empty deck under it is an answer.
    const seenSite = new Map();
    state.siteList.forEach(x => {
      if (x && x.oid) seenSite.set(x.oid, { oid: x.oid, name: x.name || x.oid, tenants: 0, fw: 0 });
    });
    const add = (oid, name) => {
      if (!seenSite.has(oid)) seenSite.set(oid, { oid, name: name || 'Unassigned', tenants: 0, fw: 0 });
      return seenSite.get(oid);
    };
    state.tenants.forEach(t => { add(t.site || '', t.site_name).tenants++; });
    state.ngfw.forEach(d => { add(d.site || '', d.site_name).fw++; });
    return [...seenSite.values()].sort((a, b) => (a.oid ? 0 : 1) - (b.oid ? 0 : 1) || a.name.localeCompare(b.name));
  }

  const tenantsForSite = () =>
    state.site ? state.tenants.filter(t => (t.site || '') === state.site) : state.tenants;
  const ngfwForSite = () =>
    state.site ? state.ngfw.filter(d => (d.site || '') === state.site) : state.ngfw;

  // address sets from the previous poll, per tenant oid — the basis for scale in/out marking.
  const seen = {};
  let timer = null;
  let pendingTimer = null;
  let ringTimer = null;      // drives the progress ring
  let composeHold = null;    // enforces the minimum on-screen time
  let composeStart = 0;
  let pendingSince = 0;      // when the current run of pending answers began
  let model = null;

  // ── Model ───────────────────────────────────────────────────────────────────
  // One tenant's getPrismaAccessIP document → zones, each with its service groups. Nothing is
  // invented here: every node in the drawing is one address the tenant actually answered with.
  function buildModel(tenant) {
    const doc = tenant && tenant.egress;
    const zonesRaw = (doc && Array.isArray(doc.result)) ? doc.result : [];
    const prev = seen[tenant.oid] || null;
    const now = Date.now() / 1000;
    const live = new Set();

    const zones = zonesRaw.map((z, zi) => {
      const groups = {};
      (Array.isArray(z.address_details) ? z.address_details : []).forEach((a) => {
        const key = a.serviceType || 'unknown';
        const g = groups[key] || (groups[key] = { svc: key, spec: svcOf(key), nodes: [], lbs: [], aux: [] });
        const node = {
          address: a.address || '',
          addressType: a.addressType || '',
          serviceType: key,
          created: Number(a.create_time) || 0,
          allowListed: a.allow_listed,
          regionalFqdn: a.ep_regional_fqdn || '',
          lbActive: a.network_load_balancer_active,
          zone: z.zone || '',
          raw: a,
        };
        node.key = (z.zone || '') + '|' + node.address;
        node.age = node.created ? Math.max(0, now - node.created) : 0;
        node.isNew = !!prev && !prev.has(node.key);
        live.add(node.key);

        if (a.addressType === 'network_load_balancer') g.lbs.push(node);
        else if (a.addressType === 'active' || a.addressType === 'service_ip' ||
                 a.addressType === 'pre_allocated') g.nodes.push(node);
        else g.aux.push(node);
      });

      const list = Object.values(groups).sort((a, b) => order(a.svc) - order(b.svc));
      const dataplane = list.some(g => g.spec.egress && (g.nodes.length || g.lbs.length));
      // Read before the placeholders go in: both are statements about what the tenant ANSWERED
      // with, and a slot standing in for something absent must not change either of them.
      const auxOnly = list.every(g => !g.nodes.length && !g.lbs.length && g.aux.length);

      // Only in the regions that carry traffic. A portal region has no MU-SPN to be missing.
      if (dataplane) {
        BASELINE_SVC.forEach((svc) => {
          if (!groups[svc]) list.push({ svc, spec: SVC[svc], nodes: [], lbs: [], aux: [], absent: true });
        });
        list.sort((a, b) => order(a.svc) - order(b.svc));
      }
      return {
        zi,
        name: z.zone || 'unnamed zone',
        geoFqdn: z.ep_geo_lb_fqdn || '',
        geoCname: z.ep_geo_lb_cname || '',
        subnets: (z.zone_subnet || []).slice(),
        subnets6: (z.zone_subnet_v6 || []).slice(),
        v6: (z.address_details_v6 || []).slice(),
        groups: list,
        dataplane,
        auxOnly,
        // How much of a region there is to see. Collapsed, the frame shows exactly one region, and
        // which one it picks is the whole impression a viewer gets of the fabric — alphabetical put
        // a one-lane region in front of a three-lane one and made the tenant look emptier than it is.
        lanes: list.filter(g => !g.absent && (g.nodes.length || g.lbs.length)).length,
        addrs: list.reduce((a, g) => a + g.nodes.length + g.lbs.length + g.aux.length, 0),
        raw: z,
      };
    });

    // Departed addresses: shown once, in the group they used to sit in, so a scale-in is visible
    // rather than silent. They are not carried into the next snapshot.
    const gone = [];
    if (prev) prev.forEach((k) => { if (!live.has(k)) gone.push(k); });
    zones.forEach((z) => {
      z.groups.forEach((g) => {
        g.goneNodes = gone.filter(k => k.indexOf(z.name + '|') === 0 &&
                                       (prevSvc[k] === g.svc)).map(k => ({ key: k, address: k.split('|')[1] }));
      });
    });

    seen[tenant.oid] = live;
    zonesRaw.forEach((z) => (z.address_details || []).forEach((a) => {
      prevSvc[(z.zone || '') + '|' + (a.address || '')] = a.serviceType || 'unknown';
    }));

    // Data-plane regions first — that is where user traffic actually lands — and among those, the
    // most populated first. Name order is the last tie-break, not the first: it is stable, which is
    // all it was ever doing here.
    zones.sort((a, b) => (b.dataplane - a.dataplane) || (a.auxOnly - b.auxOnly) ||
                         (b.lanes - a.lanes) || (b.addrs - a.addrs) || a.name.localeCompare(b.name));
    zones.forEach((z, i) => { z.zi = i; });

    const counts = { gw: 0, swg: 0, portal: 0, rn: 0, lb: 0, egressIps: [] };
    zones.forEach((z) => z.groups.forEach((g) => {
      counts.lb += g.lbs.length;
      if (g.svc === 'gp_gateway') counts.gw += g.nodes.length;
      if (g.svc === 'swg_proxy') counts.swg += g.nodes.length;
      if (g.svc === 'gp_portal') counts.portal += g.nodes.length;
      if (g.svc === 'remote_network') counts.rn += g.nodes.length;
      if (g.spec.egress) g.nodes.forEach(n => counts.egressIps.push({ address: n.address, zone: z.name }));
    }));

    // The regions that carry user traffic are the picture; portals and the global auth cache are
    // supporting cast and get a compact strip rather than a lane of their own.
    const dataZones = zones.filter(z => z.dataplane);
    const ctlZones = zones.filter(z => !z.dataplane);

    return { tenant, zones, dataZones, ctlZones, counts };
  }

  const prevSvc = {};   // node key → serviceType, so a departed address keeps its lane
  const order = (svc) => (svc === 'gp_gateway' ? 0 : svc === 'swg_proxy' ? 1 : svc === 'gp_portal' ? 2 : 3);

  const relAge = (secs) => {
    if (!secs) return '';
    const m = Math.floor(secs / 60);
    if (m < 60) return m + 'm';
    const h = Math.floor(m / 60);
    if (h < 48) return h + 'h';
    return Math.floor(h / 24) + 'd';
  };

  const visibleZone = (z) => state.region === 'all' || z.name === state.region;

  // ── Render: control strip ───────────────────────────────────────────────────
  function barHtml() {
    const sites = siteList();
    const opts = [`<option value="" ${state.site ? '' : 'selected'}>Overview</option>`].concat(
      sites.filter(x => x.oid).map(x =>
        `<option value="${esc(x.oid)}" ${state.site === x.oid ? 'selected' : ''}>${esc(x.name)}</option>`)
    ).join('');

    const t = tenantsForSite();
    const fw = ngfwForSite();
    const scope = `${t.length} SASE · ${fw.length} NGFW`;

    return `<div class="topo-bar">
        <div class="topo-bar-group">
          <span class="topo-bar-label">Site</span>
          <select class="topo-select" id="topoSite">${opts}</select>
          <span class="topo-stamp">${esc(scope)}</span>
        </div>
        <span class="topo-bar-spacer"></span>
        <button class="topo-toggle ${state.flow ? 'on' : ''}" id="topoFlow" type="button">
          <span class="topo-live-dot"></span>Flow</button>
        <button class="topo-toggle ${state.live ? 'on' : ''}" id="topoLive" type="button"
                title="Refreshes this page every ${REFRESH_LABEL} — the same refresh as the button in the title bar">
          <span class="topo-live-dot"></span>Live · ${REFRESH_LABEL}</button>
        <span class="topo-stamp">${freshness()}</span>
      </div>`;
  }

  // There is no scope-wide "last polled" summary. One number over a whole site could only ever be
  // the newest contributing timestamp, and that is three unsynchronised cycles deep — the collectord
  // probe, mgmtd's topology cache, and this page's own refresh — so it reads as a couple of minutes
  // behind even when nothing is wrong. Each lane and each node states its own age instead, which is
  // the number an operator can actually act on. Only a failed refresh still speaks here.
  function freshness() {
    if (state.error) return `<b style="color:var(--red)">${esc(state.error)}</b> — showing last known`;
    return '';
  }

  function ageSeconds(iso) {
    const t = Date.parse(String(iso).replace(/([+-]\d{2})$/, '$1:00'));
    return isFinite(t) ? Math.max(0, (Date.now() - t) / 1000) : 0;
  }

  function relStamp(iso) {
    // postgres `OF` renders a whole-hour offset as "+09"; Date.parse wants "+09:00".
    const t = Date.parse(String(iso).replace(/([+-]\d{2})$/, '$1:00'));
    if (!isFinite(t)) return iso;
    const s = Math.max(0, (Date.now() - t) / 1000);
    if (s < 60) return Math.round(s) + 's ago';
    if (s < 3600) return Math.round(s / 60) + 'm ago';
    return window.NMS.utils.fmtTs(new Date(t));
  }

  // ── Render: the two outer columns ───────────────────────────────────────────
  // How many, not which. These four cards all describe things a tenant will have several of, and a
  // card that lists them becomes a table that grows with the estate — at four Service Connections it
  // is already taller than the picture can afford, and it still cannot show a connection's subnets,
  // its tunnel or its region without becoming taller again. So the card answers the one question it
  // can answer at a glance — is there any, and how many — and the detail mark opens everything else.
  // Counts also age well: they are the same height on the day the tenant has forty.
  // A line of figures, not a row of boxes. Each count is already sitting inside a card with a border
  // and a title, so putting every number in a pill of its own was a third frame around a single
  // digit — chrome competing with the one thing worth reading. The number takes the card's own
  // colour and the word after it stays quiet, which is the whole hierarchy this needs.
  const cnt = (n, one, many) =>
    `<span class="topo-stat"><b>${n}</b>${esc(n === 1 ? one : (many || one + 's'))}</span>`;
  const cntHealth = (up, total, word) => total
    ? `<span class="topo-stat is-health"><span class="topo-dot ${up === total ? 'ok' : 'bad'}"></span>
         <b>${up}/${total}</b>${esc(word)}</span>`
    : '';
  const cnts = (...items) => `<div class="topo-stats">${items.filter(Boolean).join('')}</div>`;

  // Icon, title, subtitle — the same block on every card, so a heading is a heading wherever it
  // appears. The icon takes the card's own tone; the words never do.
  const cardHead = (icon, title, sub) => `<div class="topo-hd">
      <span class="topo-hd-ic"><svg viewBox="0 0 24 24">${icon}</svg></span>
      <span class="topo-hd-x">
        <span class="topo-hd-t">${esc(title)}</span>
        ${sub ? `<span class="topo-hd-s">${esc(sub)}</span>` : ''}
      </span>
    </div>`;

  function endCard(id, tone, icon, name, sub, body, pending, det) {
    return `<div class="topo-end ${pending ? 'is-pending' : ''}" id="${id}"
                 style="--topo-tone:var(--tc-${tone})">
        ${det ? detailBtn(det, '', name) : ''}
        ${cardHead(icon, name, sub)}
        <div class="topo-end-body">${body}</div>
      </div>`;
  }

  function edgeColumn(m) {
    const c = m.counts;
    // Endpoint kinds, not lanes: one host can be several of these at once, which is exactly how a
    // GlobalProtect tunnel and a browser proxy session end up stacked on each other.
    const mu = endCard('end-mu', 'gw', ICONS.mobile, 'Mobile Users', 'MU · three ways in', `
        <div class="topo-lane" id="ep-gp"><span class="topo-mini-dot" style="--topo-tone:var(--tc-gw)"></span>
          <span class="topo-lane-nm">GlobalProtect app</span><span class="topo-lane-l">L4</span></div>
        <div class="topo-lane" id="ep-pab"><span class="topo-mini-dot" style="--topo-tone:var(--tc-swg)"></span>
          <span class="topo-lane-nm">Prisma Access Browser</span><span class="topo-lane-l">L7</span></div>
        <div class="topo-lane" id="ep-pac"><span class="topo-mini-dot" style="--topo-tone:var(--tc-swg)"></span>
          <span class="topo-lane-nm">Browser + PAC</span><span class="topo-lane-l">L7</span></div>`,
        false, 'mu');

    // Remote networks are NOT waiting on another API: this one reports serviceType remote_network for
    // every region a branch is onboarded to, and those show up as RN-SPN inside the regions. With
    // none onboarded there is no RN-SPN to draw and no traffic to imply, so the endpoint sits here
    // disabled and unconnected rather than as a lane that looks like it is waiting for something.
    const rn = c.rn
      ? endCard('end-rn', 'rn', ICONS.branch, 'Remote Users', 'RN · users behind a branch', `
          <div class="topo-end-line"><span class="topo-mini-dot" style="--topo-tone:var(--tc-rn)"></span>
            RN-SPN addresses <b>${c.rn}</b></div>`, false, 'rn')
      : endCard('end-rn', 'pending', ICONS.branch, 'Remote Users', 'RN · users behind a branch', `
          <div class="topo-end-line"><span class="topo-tag pending">not configured</span></div>`, true, 'rn');

    return `<div class="topo-col topo-col-edge">${mu}${rn}</div>`;
  }

  function destColumn(m) {
    // An allow-list is written per region as often as it is written whole, so each address says where
    // it egresses from. Not folded: this column is one card in a full-height lane, so the fold was
    // hiding most of the answer in a card that had the room for all of it — and the answer to "which
    // source IPs must my allow-list carry" is the list, not its first four. The list takes the
    // column's slack and scrolls inside itself only for a tenant with more addresses than the column
    // is tall, which keeps the card's height a property of the layout rather than of the estate.
    const ips = m.counts.egressIps;
    const rows = ips.map(x => `<span class="topo-ip">${esc(x.address)}
        <em>(${esc(x.zone)})</em></span>`).join('');
    const net = endCard('dst-net', 'ok', ICONS.globe, 'Internet & SaaS', 'egress from the fabric', `
        <div class="topo-end-line">Egress addresses <b>${ips.length}</b></div>
        <div class="topo-ips">${rows}</div>`, false, 'net');

    // Only the destination that is NOT the customer's own estate. Everything private — the two
    // hand-offs, the firewall, and what sits behind it — stacks in the middle column, so the private
    // half of the picture reads top to bottom in one place instead of jumping a column mid-thought.
    return `<div class="topo-col topo-col-dest">${net}</div>`;
  }

  // ── Render: a zone lane ─────────────────────────────────────────────────────
  // An NLB has three states, and they are not "on / off". The tenant says `false` explicitly for a
  // GlobalProtect gateway whose IP-Optimization NLB is allocated but not carrying traffic; for an
  // explicit-proxy NLB it says nothing at all — that one is the published regional entry point
  // (ep_regional_fqdn), not an optional layer, so there is no flag to report. Rendering an absent
  // flag as "active" would be inventing an answer the API never gave.
  function lbState(n) {
    if (n.lbActive === true) return 'load balancer · active';
    if (n.lbActive === false) return 'allocated · not in use';
    return n.regionalFqdn ? 'load balancer · regional' : 'load balancer · state not reported';
  }

  function nodeCard(n, cls) {
    const sub = n.addressType === 'network_load_balancer'
      ? lbState(n)
      : (ADDR_LABEL[n.addressType] || n.addressType || 'node');
    const age = relAge(n.age);
    return `<button class="topo-node ${cls}" data-node="${esc(n.key)}" type="button"
              title="${esc(n.address + ' · ' + sub + ' · ' + n.serviceType)}">
        <span class="topo-node-ip">${esc(n.address)}</span>
        <span class="topo-node-sub">${esc(sub)}${age ? ' · ' + esc(age) : ''}</span>
      </button>`;
  }

  // An address the tenant stopped answering with, shown once in the lane it used to sit in.
  const goneCardsOf = (g) => (g.goneNodes || []).map(x =>
    `<span class="topo-node is-gone"><span class="topo-node-ip">${esc(x.address)}</span>
       <span class="topo-node-sub">withdrawn</span></span>`).join('');

  function svcGroup(z, g, gi) {
    // A slot the tenant has not bought. It keeps its lane's tone and its id, so it still occupies
    // the row a configured service would — but it carries no NLB/Nodes columns to imply an empty
    // inventory, and edgeSpecs draws nothing into it. The one thing it does carry is a withdrawal:
    // a lane becomes an empty slot exactly when its last address went away, which is the moment
    // worth seeing rather than the moment to go quiet.
    if (g.absent) {
      const gone = goneCardsOf(g);
      return `<div class="topo-svc tone-${g.spec.tone} is-absent" id="svc-${z.zi}-${gi}">
          <div class="topo-svc-h">
            <div class="topo-svc-nm" title="${esc(g.spec.label)}">${esc(g.spec.label)}</div>
            <div class="topo-svc-sub">${esc(g.spec.sub)}</div>
            ${g.spec.note ? `<div class="topo-svc-note">${esc(g.spec.note)}</div>` : ''}
          </div>
          <div class="topo-slot"><span class="topo-tag pending">not configured</span></div>
          ${gone ? `<div class="topo-nodes">${gone}</div>` : '<span></span>'}
        </div>`;
    }

    const lbCards = g.lbs.map(n => nodeCard(n, 'is-lb' + (n.lbActive === false ? ' is-standby' : '') +
                                               (n.isNew ? ' is-new' : ''))).join('')
      || '<span class="topo-empty-slot">direct</span>';

    const nodeCards = g.nodes.map(n => nodeCard(n, n.isNew ? 'is-new' : '')).join('');
    const auxCards = g.aux.map(n => nodeCard(n, 'is-aux' + (n.isNew ? ' is-new' : ''))).join('');
    const goneCards = goneCardsOf(g);

    const body = (nodeCards || auxCards || goneCards)
      ? nodeCards + auxCards + goneCards
      : '<span class="topo-empty-slot">no nodes</span>';

    // A group with nothing but auxiliary addresses is not a regional service — the tenant's global
    // auth cache arrives under swg_proxy, and calling it "proxy nodes" would be wrong.
    const auxOnly = !g.nodes.length && !g.lbs.length && g.aux.length;
    const note = auxOnly ? 'shared global service, not a regional node' : g.spec.note;

    return `<div class="topo-svc tone-${auxOnly ? 'other' : g.spec.tone}" id="svc-${z.zi}-${gi}">
        <div class="topo-svc-h">
          <div class="topo-svc-nm" title="${esc(auxOnly ? (ADDR_LABEL[g.aux[0].addressType] || g.spec.label) : g.spec.label)}">${
            esc(auxOnly ? (ADDR_LABEL[g.aux[0].addressType] || g.spec.label) : g.spec.label)}</div>
          ${g.spec.sub && !auxOnly ? `<div class="topo-svc-sub">${esc(g.spec.sub)}</div>` : ''}
          ${note ? `<div class="topo-svc-note">${esc(note)}</div>` : ''}
        </div>
        <div class="topo-slot"><span class="topo-slot-l">NLB</span>${lbCards}</div>
        <div class="topo-slot"><span class="topo-slot-l">Nodes · ${g.nodes.length + g.aux.length}</span>
          <div class="topo-nodes">${body}</div></div>
      </div>`;
  }

  function zoneLane(z) {
    // Not the absent slots: a region whose GlobalProtect lane is a placeholder is a proxy region,
    // and its header must not count nodes that are not there.
    const gw = z.groups.find(g => g.svc === 'gp_gateway' && !g.absent);
    const swg = z.groups.find(g => g.svc === 'swg_proxy' && !g.absent);
    const counts = [
      gw ? `<span class="topo-count"><span class="topo-mini-dot" style="--topo-tone:var(--tc-gw)"></span>GW <b>${gw.nodes.length}</b></span>` : '',
      swg ? `<span class="topo-count"><span class="topo-mini-dot" style="--topo-tone:var(--tc-swg)"></span>SWG <b>${swg.nodes.length}</b></span>` : '',
      `<span class="topo-count"><span class="topo-mini-dot" style="--topo-tone:var(--tc-lb)"></span>NLB <b>${z.groups.reduce((a, g) => a + g.lbs.length, 0)}</b></span>`,
    ].filter(Boolean).join('');

    const added = z.groups.reduce((a, g) => a + g.nodes.filter(n => n.isNew).length + g.lbs.filter(n => n.isNew).length, 0);
    const removed = z.groups.reduce((a, g) => a + (g.goneNodes || []).length, 0);
    const delta = (added ? `<span class="topo-delta up">+${added}</span>` : '') +
                  (removed ? `<span class="topo-delta down">−${removed}</span>` : '');

    return `<div class="topo-zone ${z.auxOnly ? 'is-global' : ''} ${visibleZone(z) ? '' : 'is-dimmed'}"
                 id="zone-${z.zi}" data-zone="${esc(z.name)}">
        <div class="topo-zone-h">
          <span class="topo-zone-nm">${esc(z.name)}</span>
          ${delta}
          <span class="topo-zone-counts">${counts}</span>
        </div>
        <div class="topo-zone-body">${z.groups.map((g, gi) => svcGroup(z, g, gi)).join('')}</div>
      </div>`;
  }

  // The private estate, in one band across the foot of the canvas: the two ways in (Service
  // Connection and ZTNA Connector — a tenant may run either or both) on top, and what they reach
  // underneath. Keeping the four together is what makes every link between them a short hop.
  // The two ways into the customer's own estate — a tenant may run either or both. They belong at
  // the foot of the fabric column: the last thing inside Prisma Access before the picture crosses to
  // on-premise.
  // The private-application hand-off. Both halves are now real: the Service Connections the tenant
  // has declared, and the ZTNA connectors it runs with whether each one's tunnel and control plane
  // are up.
  //
  // The two are read from different APIs and mean different things. The ZTNA read reports HEALTH —
  // it is why that half carries dots. The Service Connection read is SCM's deployment config: it
  // says which connections exist and how each is built, and nothing about whether one is carrying
  // traffic. That half therefore carries no dots; inventing a green one would be the worst kind of
  // wrong, since a Service Connection is exactly the thing an operator checks when the data centre
  // has gone unreachable.
  //
  // A connector is drawn as its own endpoint, not as something hanging off a firewall. They run on
  // hosts behind the customer's network and dial OUT to the fabric themselves; the firewall they sit
  // behind is not a peer and there is no link to draw between them.
  // The ZTNA half. It sits beside the Service Connection because a tenant may run either or both and
  // the pair is the answer to one question — how does the fabric reach the private estate. What sets
  // them apart is not where they are drawn but what is drawn FROM them: the Service Connection lands
  // on the firewall, and the connector does not. That difference is carried entirely by its link,
  // which leaves the stack sideways rather than dropping through it.
  function ztnaLane(tenant) {
    const z = (tenant && tenant.ztna) || {};
    const groups = Array.isArray(z.groups) ? z.groups : [];
    const conns = Array.isArray(z.connectors) ? z.connectors : [];

    const healthy = conns.filter(c => (c.flags || {}).tunnel_up && (c.flags || {}).control_plane_up).length;
    const have = groups.length || conns.length;

    const ztnaBody = have
      ? cnts(cnt(conns.length, 'connector'), cnt(groups.length, 'group'),
             cntHealth(healthy, conns.length, 'up'))
      : `<span class="topo-tag pending">not configured</span>`;

    return `<div class="topo-half topo-pa ${have ? '' : 'is-pending'}" id="dst-ztna">
        ${detailBtn('ztna', '', 'ZTNA Connector')}
        ${cardHead(ICONS.plug, 'ZTNA Connector', 'ZTT · dials out from your network')}
        ${ztnaBody}
      </div>`;
  }

  // The private estate, top to bottom: the two hand-offs side by side, the firewall the IPsec one
  // lands on, and what sits behind that.
  function handoffSplit(tenant) {
    const s = (tenant && tenant.sc) || {};

    const scConns = Array.isArray(s.connections) ? s.connections : [];
    // Names, not counts, for this one. A Service Connection is the thing an operator asks for by
    // name when the data centre has gone quiet, and its region is the other half of that name — two
    // values that identify it, where "1 connection" identifies nothing. Two chips is the budget:
    // enough to name a small tenant outright, and past that the count in the overflow chip says how
    // much more the drawer holds. Everything else about them — tunnel, subnets, SNAT, BGP — is a
    // list, and lists live in the drawer.
    // A connection is BUILT when the peer address on its IKE gateway is an address configured on a
    // firewall we manage — the two ends naming each other, each from the side that owns the fact.
    // topologyd resolves that (service-connection → ipsec_tunnel → ike gateway → peer_address) and
    // matches it; the page is handed the answer.
    //
    // "built" and not "up". Nothing collected here reports tunnel state, IKE phase or a byte
    // counter, so this says traffic CAN flow, not that it IS flowing. On the one card an operator
    // opens when the data centre has gone quiet, that distinction is the whole value of the card.
    const scBody = scConns.length
      ? `<div class="topo-chips">${scConns.slice(0, 2).map((c) => {
            const nm = c.name || c.id || 'connection';
            const where = c.region || c.region_tag || '';
            const why = c.linked
              ? 'built — peer ' + c.peer_ip + ' is ' + (c.linked_device_name || 'a managed firewall') +
                ' ' + (c.linked_interface || '')
              : c.peer_ip ? 'peer ' + c.peer_ip + ' is not an address on any firewall in this scope'
              : c.peer_fqdn ? 'peer is an FQDN (' + c.peer_fqdn + '), which cannot be matched'
              : c.peer_dynamic ? 'peer address is dynamic, so there is nothing to match'
              : 'no peer address resolved from this connection';
            return `<span class="topo-chip" title="${esc(nm + (where ? ' · ' + where : '') + ' · ' + why)}">
                <span class="topo-mini-dot" style="--topo-tone:var(${
                  c.linked ? '--tc-ok' : '--tc-faint'})"></span>
                <span class="topo-chip-nm">${esc(nm)}</span>
                ${where ? `<span class="topo-chip-z">${esc(where)}</span>` : ''}
              </span>`;
          }).join('')}${
          scConns.length > 2 ? `<span class="topo-chip is-more">+${scConns.length - 2}</span>` : ''}</div>`
      : `<span class="topo-tag pending">not configured</span>`;

    // No wrapper. The three of these are cards in the middle column exactly as the two bands above
    // them are, and nesting them in a stack of their own gave that stack its own spacing — so the
    // column ran at one rhythm down to the hand-offs and a different one below them. Flattened, one
    // gap governs the whole column and every card in it is spaced like every other.
    return `<div class="topo-priv-split">
        <div class="topo-half topo-pa ${scConns.length ? '' : 'is-pending'}" id="dst-sc">
          ${detailBtn('sc', '', 'Service Connection')}
          ${cardHead(ICONS.link, 'Service Connection', 'SC-CAN · IPsec into your network')}
          ${scBody}
        </div>
        ${ztnaLane(tenant)}
      </div>`;
  }

  // The customer's own estate: the firewall a Service Connection lands on, and what sits behind it.
  // Outside the Prisma Access band above, because that boundary is the one fact this deck is most
  // often asked for — whose kit is this, and where does someone else's service end.
  function onPremCards() {
    // Named, like the Service Connection above it — and for the same reason. A firewall is the one
    // thing on this deck the operator configured themselves, under a name they chose in the console,
    // and "1 firewall" tells them nothing they did not already know. The name, the address it is
    // reached at, and whether the last probe answered: that is the card. Everything else — ports,
    // tunnels, IKE gateways, peers — is a list, and lists are in the drawer.
    //
    // The dot is the ICMP probe's verdict and nothing more. Green means the box answered; amber
    // means it did not; grey means it has not been probed yet, which is not the same as down and
    // must not be drawn as if it were.
    const devs = ngfwForSite();
    const fwTone = (d) => d.status === 'active' ? 'var(--tc-ok)'
                        : d.status === 'down' ? 'var(--tc-warn)' : 'var(--tc-faint)';
    const fwWord = (d) => d.status === 'active' ? 'reachable'
                        : d.status === 'down' ? 'unreachable' : 'not probed yet';
    const fwBody = devs.length
      ? `<div class="topo-chips">${devs.slice(0, 2).map(d => {
            const nm = d.name || d.target || 'unnamed';
            return `<span class="topo-chip" title="${esc(nm + ' · ' + (d.target || '') + ' · ' + fwWord(d))}">
                <span class="topo-mini-dot" style="--topo-tone:${fwTone(d)}"></span>
                <span class="topo-chip-nm">${esc(nm)}</span>
                ${d.target ? `<span class="topo-chip-z">${esc(d.target)}</span>` : ''}
              </span>`;
          }).join('')}${
          devs.length > 2 ? `<span class="topo-chip is-more">+${devs.length - 2}</span>` : ''}</div>`
      : `<span class="topo-tag pending">none in scope</span>`;

    return `${endCard('dst-fw', 'ok', ICONS.shield, 'On-premise NGFW', '', fwBody, false, 'fwcard')}
      ${endCard('dst-apps', 'pending', ICONS.server, 'Private apps & Data Center', '', `
        <div class="topo-end-line"><span class="topo-tag pending">not configured</span></div>`, true, 'apps')}`;
  }

  // Collapsed does not mean empty: one region stays on screen so the shape of a region is still
  // legible, and the rest are counted below it.
  const shownZones = (m) => state.planeOpen ? m.dataZones : m.dataZones.slice(0, 1);

  // The regions are the tallest thing on the page, and an operator watching the private hand-off or
  // comparing regions at the summary level does not always want all of it. Collapsed, the frame keeps
  // its counts and its links — it simply stops listing every node.
  function planeFrame(m) {
    const open = state.planeOpen;
    const shown = shownZones(m);
    const hidden = m.dataZones.length - shown.length;

    // What the frame is hiding is said once, in the header, beside the control that reveals it — a
    // second row at the foot repeating it was the same sentence twice and cost the drawing a line.
    // The census that used to sit at the right is in the detail panel; on the canvas it was four
    // numbers nobody was reading against anything.
    const rest = m.dataZones.slice(shown.length).map(z => z.name).join(', ');

    return `<div class="topo-plane topo-pa ${open ? '' : 'is-closed'}" id="data-plane">
        ${detailBtn('plane', '', 'Data plane · traffic regions')}
        <button class="topo-plane-h" id="planeToggle" type="button" aria-expanded="${open}"
                title="${esc(open ? 'Show one region' : rest ? 'Show ' + rest : '')}">
          ${cardHead(ICONS.layers, 'Data Plane', 'traffic regions · where user sessions land')}
          ${m.dataZones.length > 1 ? `<span class="topo-plane-more">
              <span class="topo-more-x" aria-hidden="true">${open ? '−' : '+'}</span>${
              open ? 'show one region' : 'view all regions'}</span>` : ''}
        </button>
        <div class="topo-plane-b">${shown.map(zoneLane).join('')}</div>
      </div>`;
  }

  // Portals and the global auth cache carry no user traffic, so they are chips at the head of the
  // column rather than lanes competing with the regions that do.
  function ctlStrip(m) {
    if (!m.ctlZones.length) return '';
    const chips = m.ctlZones.map((z, i) => z.groups.map((g, gi) => {
      const n = g.nodes.length + g.aux.length;
      const aux = !g.nodes.length && g.aux.length;
      return `<span class="topo-chip" id="ctl-${i}-${gi}" title="${esc(z.name + ' · ' + g.svc)}">
          <span class="topo-mini-dot" style="--topo-tone:var(--tc-${aux ? 'other' : 'portal'})"></span>
          <span class="topo-chip-nm">${esc(aux ? 'Auth cache' : 'GP Portal')}</span>
          <span class="topo-chip-z">${esc(z.name)}</span>
          <b>${n}</b>
        </span>`;
    }).join('')).join('');

    // Headed like the data-plane frame below it, down to where the label starts: the two are the
    // same kind of thing — a band of the fabric with a name on it — and reading them as a pair
    // depends on them being titled identically rather than merely similarly.
    return `<div class="topo-ctl topo-pa" id="ctl-strip">
        ${detailBtn('ctl', '', 'Control plane & shared services')}
        ${cardHead(ICONS.sliders, 'Control Plane', 'portals and auth · carries no user traffic')}
        <div class="topo-chips">${chips}</div>
      </div>`;
  }

  // ── Render: page ────────────────────────────────────────────────────────────
  function canvasHtml(m) {
    // The fabric deck draws ONE tenant — a Prisma fabric is per-tenant and two of them share no
    // regions, no addresses and no lanes, so there is nothing coherent to overlay. Under "All sites"
    // it was quietly drawing whichever tenant sorted first while the header said "all", which reads
    // as "this is your estate" when it is one site of several. Ask instead.
    //
    // Exactly one <b>: in .topo-msg it is the title style (display:block), so a second one becomes a
    // stray heading mid-sentence rather than emphasis.
    if (!state.site) {
      // Not a blocked page — the estate at rest. Unscoped, topologyd answers with every site's
      // tenants and firewalls, so this can total them up and let each site be entered from its own
      // row. Rows, not a card grid: one card in a grid reads as a layout that failed, one row reads
      // as a list with one thing in it, and rows go on working at forty.
      //
      // Not wrapped in .topo-stage: that class carries the drawing's min-width and asymmetric
      // padding, which push this off-centre.
      const seen = new Map();
      (state.siteList || []).forEach(x => {
        if (x && x.oid) seen.set(x.oid, { oid: x.oid, name: x.name || x.oid, tenants: 0, fw: 0 });
      });
      const ensure = (oid, name) => {
        if (!oid) return null;
        if (!seen.has(oid)) seen.set(oid, { oid, name: name || oid, tenants: 0, fw: 0 });
        return seen.get(oid);
      };
      state.tenants.forEach(t => { const e = ensure(t.site, t.site_name); if (e) e.tenants++; });
      state.ngfw.forEach(d => { const e = ensure(d.site, d.site_name); if (e) e.fw++; });

      const sites = [...seen.values()].sort((a, b) => a.name.localeCompare(b.name));
      if (!sites.length) {
        return `<div class="topo-msg is-full"><div><b>No site configured</b>
          Add one in <a href="settings?tab=sites">Configuration › Sites</a>.</div></div>`;
      }

      const tile = (v, label) => `<div class="topo-ov-tile"><b>${v}</b><span>${esc(label)}</span></div>`;
      const stat = (v, label) => `<span class="topo-ov-stat"><b>${v}</b>${esc(label)}</span>`;

      const rows = sites.map(x => `<button class="topo-ov-row" type="button" data-site="${esc(x.oid)}">
          <span class="topo-ov-nm">${esc(x.name)}</span>
          <span class="topo-ov-stats">
            ${stat(x.tenants, 'SASE')}
            ${stat(x.fw, 'NGFW')}
          </span>
          <span class="topo-ov-go">&rsaquo;</span>
        </button>`).join('');

      return `<div class="topo-overview">
          <div class="topo-ov-h">Overview</div>
          <div class="topo-ov-tiles">
            ${tile(sites.length, sites.length === 1 ? 'site' : 'sites')}
          </div>
          <div class="topo-ov-list">${rows}</div>
          <div class="topo-ov-note">Open a site to draw its fabric and its firewalls.</div>
        </div>`;
    }

    if (state.booting && state.site) return `<div class="topo-msg is-full"></div>`;

    if (!m || !m.zones.length) {
      const t = m && m.tenant;
      return `<div class="topo-stage"><div class="topo-msg">
          <div><b>${t ? 'No infrastructure answer yet' : 'No SASE tenant in this scope'}</b>
          ${t
            ? `${esc(t.name || t.target)} has not returned a getPrismaAccessIP document yet. Run the
               Infrastructure Test from <a href="settings?tab=devices">Configuration › Devices</a>, or
               wait for the next probe cycle.`
            : `This site has no SASE device. Add one in
               <a href="settings?tab=devices">Configuration › Devices</a> and run its Infrastructure
               Test — the fabric is drawn from what the tenant answers.`}</div>
        </div></div>`;
    }

    const regions = m.dataZones.length;
    const ips = m.counts.egressIps.length;

    return `<div class="topo-stage" id="topoStage">
        <svg class="topo-links" id="topoLinks" xmlns="http://www.w3.org/2000/svg"></svg>
        <div class="topo-deck-h">
          <span class="topo-deck-t">SASE Infrastructure</span>
          <span class="topo-deck-sum">${esc(m.tenant.name || m.tenant.target || 'tenant')}
            · <b>${regions}</b> data plane region${regions === 1 ? '' : 's'}
            · <b>${ips}</b> egress IP address${ips === 1 ? '' : 'es'}</span>
        </div>
        <div class="topo-cols">
          ${edgeColumn(m)}
          <div class="topo-col">
            <!-- The band is the answer to the question this deck is asked most: whose kit is this?
                 Everything inside it is a service Palo Alto runs and an operator cannot log into;
                 everything below it is in the customer's own racks. The four cards already carried
                 that in their colours, but a colour has to be learned — a named ground states it. -->
            <div class="topo-pa-zone">
              <span class="topo-pa-zone-t">Prisma Access</span>
              ${ctlStrip(m)}
              ${planeFrame(m)}
              ${handoffSplit(m.tenant)}
            </div>
            ${onPremCards()}
          </div>
          ${destColumn(m)}
        </div>
      </div>`;
  }

  // ── Deck 2: on-premise infrastructure ───────────────────────────────────────
  // The fabric deck draws the service someone else runs; this one draws the boxes in the customer's
  // own racks and — the part that makes it a topology rather than an inventory — where each of them
  // reaches. It reads left to right the same way the fabric deck does, so moving between the two
  // does not mean relearning the picture:
  //
  //   access edge          firewalls              destinations
  //   GP portals/gateways  the box, with its      SASE tenants (Service Connections and Remote
  //   the remote users     interfaces as ports    Networks, grouped by tenant) and external peers
  //
  // Every edge on this deck comes from an IKE gateway's peer address. That is a fact the firewall
  // states about itself, so a line here means "this box is configured to reach that" — not "these
  // two things look related", which is what a picture assembled from addresses alone would mean.
  const LAYER_WORD = { ok: 'active', fail: 'inactive', unknown: 'not configured' };

  // Above this many firewalls the ports stop being readable at a glance and the deck switches to one
  // row per box, expandable. It is a default rather than a rule: the toggle in the deck header
  // overrides it in both directions, because "20 firewalls but I want to see all the ports" is a
  // legitimate thing to want and the layout can do it.
  const DENSE_ABOVE = 4;

  const isDense = () => {
    if (state.ngfwDense !== null) return state.ngfwDense;
    return ngfwForSite().length > DENSE_ABOVE;
  };

  function layerDots(oid) {
    const l = state.layers[oid] || {};
    const names = [['reachable', 'Device — ICMP reachability'], ['credential', 'Credential — API key'],
                   ['api', 'API — collection call']];
    return names.map(([k, title]) => {
      const v = l[k] || 'unknown';
      return `<span class="topo-lyr ${v}" title="${esc(title + ': ' + (LAYER_WORD[v] || v))}"></span>`;
    }).join('');
  }

  // Links belonging to one firewall, and the ones that reach the fabric specifically. A firewall
  // with fabric links is doing something the picture cares about; one with only external peers is a
  // perfectly normal edge firewall and is drawn as one.
  const linksOf = (oid) => (state.links || []).filter(l => l.device === oid);
  const isFabric = (l) => l.kind === 'service_connection' || l.kind === 'remote_network';

  // ── Destinations ────────────────────────────────────────────────────────────
  // Grouped by tenant, because that is the unit that means something: two Service Connections to the
  // same tenant are two paths into one fabric, and to different tenants are two fabrics. The tenant
  // token is the only thing in the collected data that can tell those apart — see topologyd's
  // classifyPeer — so it is what the grouping is keyed on rather than the region or the name.
  function destGroups() {
    const tenants = new Map();
    const external = [];

    (state.links || []).forEach((l) => {
      if (!isFabric(l)) { external.push(l); return; }
      const key = l.tenant || 'unknown';
      if (!tenants.has(key)) tenants.set(key, { key, sc: [], rn: [], devices: new Set() });
      const g = tenants.get(key);
      (l.kind === 'service_connection' ? g.sc : g.rn).push(l);
      g.devices.add(l.device);
    });

    return { tenants: [...tenants.values()].sort((a, b) => b.devices.size - a.devices.size), external };
  }

  function destCard(g, hub) {
    const row = (l, kind) => `<span class="topo-dest-r kind-${kind}">
        <span class="topo-dest-k">${kind === 'sc' ? 'SC' : 'RN'}</span>
        <span class="topo-dest-nm" title="${esc(l.peer)}">${esc(l.label || l.peer)}</span>
        ${l.region ? `<span class="topo-dest-rg">${esc(l.region)}</span>` : ''}
      </span>`;

    // Deduplicated by the endpoint's own name: several firewalls reaching the same Service
    // Connection is one destination reached twice, not two destinations.
    const uniq = (arr) => {
      const seenNm = new Set();
      return arr.filter(l => !seenNm.has(l.label || l.peer) && seenNm.add(l.label || l.peer));
    };

    return `<div class="topo-dest ${hub ? 'is-hub' : ''}" id="ndest-${esc(g.key)}">
        ${detailBtn('dest', g.key, g.key === 'unknown' ? 'Prisma Access tenant' : g.key)}
        <div class="topo-dest-h">
          <span class="topo-dest-ic"><svg viewBox="0 0 24 24">${ICONS.cloud}</svg></span>
          <span>
            <span class="topo-dest-t">Prisma Access</span>
            <span class="topo-dest-sub">tenant <code>${esc(g.key)}</code></span>
          </span>
          ${hub ? '<span class="topo-hub-tag">hub</span>' : ''}
        </div>
        <div class="topo-dest-b">
          ${uniq(g.sc).map(l => row(l, 'sc')).join('')}
          ${uniq(g.rn).map(l => row(l, 'rn')).join('')}
        </div>
        <div class="topo-dest-f">${g.devices.size} firewall${g.devices.size === 1 ? '' : 's'} attached</div>
      </div>`;
  }

  function externalCard(list) {
    if (!list.length) return '';
    // One row per distinct peer. A partner reached by three firewalls is one partner.
    const byPeer = new Map();
    list.forEach(l => { if (!byPeer.has(l.peer)) byPeer.set(l.peer, l); });

    return `<div class="topo-dest is-ext" id="ndest-external">
        ${detailBtn('ext', '', 'External peers')}
        <div class="topo-dest-h">
          <span class="topo-dest-ic"><svg viewBox="0 0 24 24">${ICONS.globe}</svg></span>
          <span>
            <span class="topo-dest-t">External peers</span>
            <span class="topo-dest-sub">not Prisma Access</span>
          </span>
        </div>
        <div class="topo-dest-b">
          ${[...byPeer.values()].map(l => `<span class="topo-dest-r kind-ext">
              <span class="topo-dest-k">VPN</span>
              <span class="topo-dest-nm mono" title="${esc(l.gateway)}">${esc(l.label || l.peer)}</span>
            </span>`).join('')}
        </div>
        <div class="topo-dest-f">${byPeer.size} peer${byPeer.size === 1 ? '' : 's'}</div>
      </div>`;
  }

  // ── The firewall box ────────────────────────────────────────────────────────
  // A port carries the two things an operator looks for first: which side of the box it is on, and
  // what address is on it. The role is topologyd's, and the badges say WHY it was called an edge —
  // an interface is WAN because a VPN or GlobalProtect terminates on it, or because its address is
  // public, and those are different confidences worth distinguishing.
  function portChip(i) {
    const badges =
      (i.vpn_count ? `<span class="topo-port-b b-vpn" title="${i.vpn_count} IKE gateway${
        i.vpn_count === 1 ? '' : 's'} terminate here">VPN</span>` : '') +
      (i.gp_count ? `<span class="topo-port-b b-gp" title="GlobalProtect listens here">GP</span>` : '');

    return `<span class="topo-port role-${esc(i.role || 'unknown')}${
        i.admin_state === 'down' ? ' is-down' : ''}"
        title="${esc(i.name + (i.mode ? ' · ' + i.mode : '') + (i.comment ? ' — ' + i.comment : ''))}">
        <span class="topo-port-n">${esc(i.name)}</span>
        <span class="topo-port-ip mono">${esc(i.ip || 'no address')}</span>
        ${badges}
      </span>`;
  }

  function fwBox(d, dense) {
    const ifc = d.interfaces || {};
    const tun = d.tunnels || {};
    const gp = d.gp || {};
    const list = ifc.list || [];
    const my = linksOf(d.oid);
    const fabricN = my.filter(isFabric).length;

    // `edge` sits on the WAN side: whatever its address says, that is where the outside arrives.
    // Its own colour keeps it from being read as a plain internet edge.
    const wan = list.filter(i => i.role === 'wan' || i.role === 'edge');
    const lan = list.filter(i => i.role === 'lan');
    const rest = list.filter(i => ['wan', 'edge', 'lan', 'tunnel'].indexOf(i.role) < 0);

    const l = state.layers[d.oid] || {};
    const st = (l.reachable === 'ok' && l.credential === 'ok') ? 'ok'
             : (l.reachable === 'fail' || l.credential === 'fail') ? 'bad' : '';
    const hub = state.shape && state.shape.kind === 'ngfw_hub' && fabricN >= 2;
    const open = !dense || state.fwOpen[d.oid];

    // An expanded box needs a way back — a box you can open and not close is a trap. Only when it
    // IS expanded, though: on a collapsed row the row itself already carries the toggle, and a
    // second target inside it is the one closest() would find, leaving the click swapping out the
    // header instead of the row.
    const head = `<div class="topo-fwb-h"${dense && open ? ` data-fwrow="${esc(d.oid)}"` : ''}>
        <span class="topo-fw-dot ${d.status === 'active' ? 'ok' : d.status === 'down' ? 'bad' : ''}"></span>
        <span class="topo-fwb-nm">${esc(d.name || d.target || 'unnamed')}</span>
        <span class="topo-fwb-t mono">${esc(d.target || '')}</span>
        ${hub ? '<span class="topo-hub-tag">hub</span>' : ''}
        <span class="topo-fwb-l">${layerDots(d.oid)}</span>
      </div>`;

    // The one-line summary. In dense mode it IS the row; expanded, it sits under the ports as the
    // count line. Either way it answers "how big is this box and how much of it faces outward".
    const sum = `<div class="topo-fwb-sum">
        ${ifc.collected
          ? `<span><b>${ifc.total || 0}</b> interfaces</span>
             <span class="topo-sum-wan"><b>${(ifc.wan || 0) + (ifc.edge || 0)}</b> WAN</span>
             <span class="topo-sum-lan"><b>${ifc.lan || 0}</b> LAN</span>`
          : '<span class="topo-sum-none">interfaces not collected</span>'}
        ${tun.collected ? `<span><b>${tun.total || 0}</b> tunnels</span>` : ''}
        ${fabricN ? `<span class="topo-sum-fab"><b>${fabricN}</b> to fabric</span>` : ''}
        ${(gp.portals || []).length || (gp.gateways || []).length
          ? `<span class="topo-sum-gp">GlobalProtect</span>` : ''}
      </div>`;

    if (!open) {
      return `<button class="topo-fwb is-row ${st}" type="button" data-fwrow="${esc(d.oid)}"
                      id="nfw-${esc(d.oid)}">${head}${sum}</button>`;
    }

    const side = (title, arr, cls) => `<div class="topo-fwb-side ${cls}">
        <div class="topo-fwb-side-t">${title}</div>
        ${arr.length ? arr.map(portChip).join('')
                     : `<span class="topo-port is-empty">none</span>`}
      </div>`;

    return `<div class="topo-fwb ${st}" id="nfw-${esc(d.oid)}" data-fwbox="${esc(d.oid)}">
        ${head}
        ${ifc.collected
          ? `<div class="topo-fwb-ports">
               ${side('LAN — inside', lan.concat(rest), 'is-lan')}
               <div class="topo-fwb-core"><span>${esc(d.name || 'firewall')}</span></div>
               ${side('WAN — edge', wan, 'is-wan')}
             </div>`
          : `<div class="topo-none-block">Interfaces have not been collected for this firewall.
               Add the ethernet-interfaces endpoint to its
               <a href="settings?tab=api-connector">API Connector</a>.</div>`}
        ${sum}
        ${detailBtn('fw', d.oid, d.name || d.target || 'firewall')}
      </div>`;
  }

  // ── The drawer's firewall view ──────────────────────────────────────────────
  // The lists behind the picture. The tunnel table gains the column it could never fill before: a
  // tunnel's peer, resolved through its IKE gateway, which is the same join the links are drawn from.
  function fwDetail(d) {
    const ifc = d.interfaces || {};
    const tun = d.tunnels || {};
    const ike = d.ike || {};
    const gp = d.gp || {};

    const peerCell = (p) => {
      if (!p || !p.kind || p.kind === 'unknown') return '<span class="topo-none">—</span>';
      const tag = p.kind === 'service_connection' ? 'SC'
                : p.kind === 'remote_network' ? 'RN' : 'ext';
      return `<span class="topo-peer k-${esc(p.kind)}"><span class="topo-peer-k">${tag}</span>${
        esc(p.label || p.addr)}</span>`;
    };

    const ifRows = (ifc.list || []).map(i => `<tr>
        <td><span class="topo-dot ${i.admin_state === 'down' ? 'bad' : 'ok'}"></span>${esc(i.name)}</td>
        <td class="mono">${esc(i.ip) || '<span class="topo-none">no address</span>'}</td>
        <td><span class="topo-rl role-${esc(i.role || 'unknown')}">${esc(i.role || 'unknown')}</span></td>
        <td class="topo-sub">${esc(i.mode || '')}</td>
      </tr>`).join('');

    const tunRows = (tun.list || []).map(t => `<tr>
        <td><span class="topo-dot ${t.enabled ? 'ok' : 'off'}"></span>${esc(t.name)}</td>
        <td class="mono">${esc(t.interface) || '—'}</td>
        <td class="topo-sub">${esc(t.gateway) || '—'}</td>
        <td>${peerCell(t.peer)}</td>
      </tr>`).join('');

    const ikeRows = (ike.list || []).map(g => `<tr>
        <td>${esc(g.name)}</td>
        <td class="mono">${esc(g.interface) || '—'}</td>
        <td class="mono topo-sub">${esc(g.local_ip) || '—'}</td>
        <td>${peerCell(g.peer)}</td>
      </tr>`).join('');

    const gpRows = ((gp.portals || []).map(p => `<tr>
        <td><span class="topo-acc-k">portal</span> ${esc(p.name)}</td>
        <td class="mono">${esc(p.interface) || '—'}</td>
        <td class="topo-sub">${esc((p.gateways || []).join(', ')) || '—'}</td>
      </tr>`).join('')) +
      ((gp.gateways || []).map(g => `<tr>
        <td><span class="topo-acc-k k-gw">gateway</span> ${esc(g.name)}</td>
        <td class="mono">${esc(g.interface) || (g.tunnel_mode ? 'tunnel mode' : '—')}</td>
        <td class="topo-sub">${esc((g.pools || []).join(', ')) || '—'}</td>
      </tr>`).join(''));

    const block = (title, collected, at, cols, rows, empty) => `
      <div class="topo-dsec">${title}
        ${collected ? `<span class="topo-dsec-age">read ${esc(relStamp(at))}</span>` : ''}</div>
      ${!collected
        ? `<div class="topo-none-block">Not collected. Add this endpoint to the firewall's
             <a href="settings?tab=api-connector">API Connector</a> and it appears here on the next cycle.</div>`
        : rows
          ? `<table class="topo-dtable"><thead><tr>${cols.map(c => `<th>${c}</th>`).join('')}</tr></thead>
             <tbody>${rows}</tbody></table>`
          : `<div class="topo-none-block">${empty}</div>`}`;

    return block('Interfaces', ifc.collected, ifc.collected_at,
                 ['Interface', 'Address', 'Role', 'Mode'], ifRows,
                 'The firewall reports no interfaces.') +
           block('IPSec tunnels', tun.collected, tun.collected_at,
                 ['Tunnel', 'Interface', 'IKE gateway', 'Peer'], tunRows,
                 'No IPSec tunnels are defined.') +
           block('IKE gateways', ike.collected, ike.collected_at,
                 ['Gateway', 'Interface', 'Local address', 'Peer'], ikeRows,
                 'No IKE gateways are defined.') +
           block('GlobalProtect', gp.collected, gp.collected_at,
                 ['Object', 'Interface', 'Gateways / client pools'], gpRows,
                 'No GlobalProtect portal or gateway is configured.');
  }

  // ── Access edge ─────────────────────────────────────────────────────────────
  // GlobalProtect is where the firewall's own remote users arrive. It belongs on the left with the
  // other inbound edges rather than beside the fabric: these users terminate ON the box, they do not
  // travel through Prisma to get there.
  function gpCard(devs) {
    const portals = [];
    const gateways = [];
    devs.forEach((d) => {
      ((d.gp || {}).portals || []).forEach(p => portals.push({ d, p }));
      ((d.gp || {}).gateways || []).forEach(g => gateways.push({ d, g }));
    });
    if (!portals.length && !gateways.length) return '';

    const pools = gateways.reduce((n, x) => n + ((x.g.pools || []).length), 0);

    return `<div class="topo-acc" id="ngp">
        ${detailBtn('gp', '', 'GlobalProtect — on-premise')}
        <div class="topo-acc-h">
          <span class="topo-acc-ic"><svg viewBox="0 0 24 24">${ICONS.mobile}</svg></span>
          <span>
            <span class="topo-acc-t">GlobalProtect</span>
            <span class="topo-acc-sub">remote users terminating on-premise</span>
          </span>
        </div>
        <div class="topo-acc-b">
          ${portals.map(({ d, p }) => `<span class="topo-acc-r">
              <span class="topo-acc-k">portal</span>
              <span class="topo-acc-nm">${esc(p.name)}</span>
              <span class="topo-acc-x mono">${esc(p.interface || '—')}</span>
            </span>`).join('')}
          ${gateways.map(({ d, g }) => `<span class="topo-acc-r">
              <span class="topo-acc-k k-gw">gateway</span>
              <span class="topo-acc-nm">${esc(g.name)}</span>
              <span class="topo-acc-x mono">${esc(g.interface || (g.tunnel_mode ? 'tunnel' : '—'))}</span>
            </span>`).join('')}
        </div>
        <div class="topo-acc-f">${portals.length} portal${portals.length === 1 ? '' : 's'} ·
          ${gateways.length} gateway${gateways.length === 1 ? '' : 's'}${
          pools ? ` · ${pools} client pool${pools === 1 ? '' : 's'}` : ''}</div>
      </div>`;
  }

  // ── The deck ────────────────────────────────────────────────────────────────
  // The shape line is the one sentence the picture is trying to say. It is derived by topologyd from
  // where the fan-out is, not configured — see the shape block there — so it changes when the estate
  // changes rather than when somebody remembers to update a label.
  const SHAPE_WORD = {
    sase_hub: ['Prisma Access is the hub',
               'each firewall reaches the fabric on its own connection — a star with the fabric at the centre'],
    ngfw_hub: ['One firewall is the hub',
               'the fabric connections are held by a single box and the rest reach it through the estate'],
    edge:     ['Edge attachment',
               'this site attaches to the fabric on one firewall'],
    flat:     ['No fabric attachment',
               'no IKE gateway on these firewalls terminates on Prisma Access'],
  };

  function ngfwDeck() {
    const all = ngfwForSite();
    if (!all.length) {
      return `<div class="topo-stage topo-stage-ngfw"><div class="topo-msg">
          <div><b>No firewall is managed here yet</b>
          Add one in <a href="settings?tab=devices">Configuration › Devices</a> — it will appear here
          and as the enforcement point on the fabric deck.</div>
        </div></div>`;
    }

    const dense = isDense();
    const dg = destGroups();
    const shape = (state.shape && state.shape.kind) || 'flat';
    const [shapeT, shapeS] = SHAPE_WORD[shape] || SHAPE_WORD.flat;

    // The hub firewall leads, so the box everything else hangs off is the first one read.
    const order = all.slice().sort((a, b) => linksOf(b.oid).filter(isFabric).length -
                                             linksOf(a.oid).filter(isFabric).length);

    let ok = 0, bad = 0, unk = 0;
    all.forEach((d) => { if (d.status === 'active') ok++; else if (d.status === 'down') bad++; else unk++; });

    const access = gpCard(all);
    const dests = dg.tenants.map(g => destCard(g, shape === 'sase_hub' && g.devices.size > 1)).join('') +
                  externalCard(dg.external);

    return `<div class="topo-stage topo-stage-ngfw" id="ngfwStage">
        <svg class="topo-links" id="ngfwLinks" aria-hidden="true"></svg>
        <div class="topo-deck-h">
          <span class="topo-deck-t">NGFW Infrastructure</span>
          <span class="topo-deck-sum">
            <b>${all.length}</b> firewall${all.length === 1 ? '' : 's'}
            · <span class="ok">${ok} reachable</span>
            ${bad ? `· <span class="bad">${bad} unreachable</span>` : ''}
            ${unk ? `· <span class="muted">${unk} not probed</span>` : ''}
          </span>
          <span class="topo-bar-spacer"></span>
          <button class="topo-toggle ${dense ? '' : 'on'}" id="ngfwDensity" type="button"
                  title="Show every interface as a port, or one row per firewall">
            ${dense ? 'Show ports' : 'Compact rows'}</button>
        </div>

        <div class="topo-shape shape-${esc(shape)}">
          <b>${esc(shapeT)}</b><span>${esc(shapeS)}</span>
        </div>

        <div class="topo-ncols">
          <div class="topo-ncol is-access">
            ${access || `<div class="topo-ncol-none">No GlobalProtect portal or gateway is configured
              on these firewalls.</div>`}
          </div>

          <div class="topo-ncol is-fw">
            ${order.map(d => fwBox(d, dense)).join('')}
          </div>

          <div class="topo-ncol is-dest">
            ${dests || `<div class="topo-ncol-none">No IKE gateway on these firewalls names a peer,
              so there is nothing to draw a link to yet. Collect the
              <code>IKEGatewayNetworkProfiles</code> endpoint to populate this.</div>`}
          </div>
        </div>

        <div class="topo-deck-note">Ports are coloured by role: <b class="k-wan">WAN</b> is a public
          address, <b class="k-edge">EDGE</b> is a private address that nonetheless terminates a VPN
          or GlobalProtect — a firewall behind an upstream NAT — and <b class="k-lan">LAN</b> is
          RFC1918 with nothing external on it. Every link is an IKE gateway's configured peer: the
          firewall stating where it reaches, not an inference.
          Layer dots: reachability · credential · API collection.</div>
      </div>`;
  }


  // The control that moves between the two decks. It names the destination, and its arrow points the
  // way the screen will travel. It sits on the side of the drawing it points at: below when it leads down to the NGFW
  // deck, above when it leads back up to the fabric. The arrow then says the same thing twice — its
  // direction and its position — and the two decks stop feeling like one page with a button on the
  // bottom, which is the thing they are deliberately not.
  const deckBarHtml = () =>
    `<div class="topo-deck-bar" id="deckBar">${deckBtn(state.view === 'fabric' ? 'ngfw' : 'fabric')}</div>`;

  // Moved rather than re-rendered when the deck changes: a full render would restage both decks to
  // relocate one control, and the slide between them is the whole effect.
  function placeDeckBar() {
    const bar = document.getElementById('deckBar');
    const canvas = document.getElementById('topoCanvas');
    if (!bar || !canvas || !canvas.parentNode)
        return;
    if (state.view === 'ngfw')
      canvas.parentNode.insertBefore(bar, canvas);
    else
      canvas.parentNode.insertBefore(bar, canvas.nextSibling);
  }

  function deckBtn(to) {
    const down = to === 'ngfw';
    return `<button class="topo-deck-btn" id="deckSwitch" type="button">
        <span>${down ? 'NGFW Infrastructure' : 'SASE Infrastructure'}</span>
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round">
          ${down ? '<path d="M12 5v14"/><polyline points="6 13 12 19 18 13"/>'
                 : '<path d="M12 19V5"/><polyline points="6 11 12 5 18 11"/>'}
        </svg>
      </button>`;
  }

  // Shown while topologyd is composing. The stages are the real pipeline — mgmtd asks, topologyd
  // reads the collected samples, correlates them, and answers — and they light in order as the
  // retries go by. The bar is honest about what it measures: it tracks attempts made, not work
  // completed, because this side cannot see inside the composer. It never reaches 100% on its own;
  // arriving data is what ends it.
  // The phase named beside the ring. The composition really does run in this order, and the ring
  // reaches each quarter as the corresponding stage is the one that would be running.
  const COMPOSE_STAGES = [
    'Requesting composition',
    'Reading collected samples',
    'Correlating tenants and firewalls',
    'Rendering\u2026',
  ];

  const RING_R = 52;
  const RING_C = 2 * Math.PI * RING_R;

  function composingHtml() {
    return `<div class="topo-composing">
        <div class="topo-ring-wrap">
          <svg class="topo-ring" viewBox="0 0 120 120" aria-hidden="true">
            <circle class="tr-track" cx="60" cy="60" r="${RING_R}"/>
            <circle class="tr-fill" cx="60" cy="60" r="${RING_R}"
                    stroke-dasharray="${RING_C.toFixed(1)}" stroke-dashoffset="${RING_C.toFixed(1)}"/>
          </svg>
          <div class="topo-ring-pct">0<small>%</small></div>
        </div>
        <div class="topo-compose-t">Composing this site&rsquo;s infrastructure</div>
        <div class="topo-compose-stage">${esc(COMPOSE_STAGES[0])}</div>
      </div>`;
  }

  // Drives the ring. The percentage is elapsed time against the hold below, so it is a real
  // countdown to the moment the picture appears — not a guess at the daemon's internal progress,
  // which this side cannot see. It reaches 100% exactly when the drawing does.
  function tickRing() {
    // Every ring on the page, not the first: both decks are laid out at all times, so a composing
    // page has two of them and an id would only ever drive one.
    const fills = document.querySelectorAll('.topo-ring .tr-fill');
    if (!fills.length) return;

    // The ring only reaches 100% once the composition is actually in hand. While still waiting it
    // stops short — a full ring over an unfinished job is the one thing a progress indicator must
    // never show, because it turns "working" into "done, but nothing happened".
    const elapsed = Date.now() - composeStart;
    const ceiling = state.answered ? 1 : 0.9;
    const pct = Math.min(ceiling, elapsed / MIN_COMPOSE_MS);
    const offset = (RING_C * (1 - pct)).toFixed(1);
    fills.forEach(f => f.setAttribute('stroke-dashoffset', offset));

    const pctHtml = Math.round(pct * 100) + '<small>%</small>';
    document.querySelectorAll('.topo-ring-pct').forEach(p => { p.innerHTML = pctHtml; });

    document.querySelectorAll('.topo-compose-stage').forEach((s) => {
      // Past the expected time with no answer, say so rather than naming a stage that finished long
      // ago — the operator is now waiting on something slow, and that is the useful fact.
      const label = (!state.answered && elapsed > MIN_COMPOSE_MS)
        ? 'Still waiting for topologyd\u2026'
        : COMPOSE_STAGES[Math.min(COMPOSE_STAGES.length - 1, Math.floor(pct * COMPOSE_STAGES.length))];
      if (s.textContent !== label) s.textContent = label;
    });
  }

  function startComposing() {
    if (state.composing) return;
    state.composing = true;
    state.answered = false;
    composeStart = Date.now();
    clearInterval(ringTimer);
    ringTimer = setInterval(tickRing, 40);
  }

  // The composition itself settles in tens of milliseconds — far too fast to read. The hold is
  // deliberate: an indicator that appears and vanishes within one frame tells the operator nothing,
  // and on the runs that DO take time (a busy or unreachable topologyd) the same indicator is the
  // only thing that explains the wait. So it always runs for its full length once shown.
  function finishComposing(then) {
    if (!state.composing) return then();
    const remaining = Math.max(0, MIN_COMPOSE_MS - (Date.now() - composeStart));
    clearTimeout(composeHold);
    composeHold = setTimeout(() => {
      clearInterval(ringTimer);
      ringTimer = null;
      state.composing = false;
      then();
    }, remaining);
  }

  function render() {
    const root = document.getElementById('contentBody');
    if (!root) return;
    const scoped = tenantsForSite();

    // Both decks are per-site: a fabric belongs to one tenant, and a firewall sits in one site. So
    // "All sites" is not a wider view of this page, it is a view this page cannot draw — and rather
    // than half-answering it with an arbitrary tenant and an undifferentiated pile of firewalls, the
    // whole apparatus stands down. One screen, one instruction, no deck switch and no legend for
    // edges that are not on screen.
    const scopeChosen = !!state.site;

    // The ring is only for a deck that has nothing to show. Once a picture is up, a refresh happens
    // underneath it — replacing a drawn estate with an animation for the 20ms a round trip takes
    // would read as the page breaking, not as it working.
    const composing = state.composing && scopeChosen;
    // Not built when no site is chosen. buildModel is not a pure function — it records each tenant's
    // address set to diff the NEXT poll against, which is how a node that appeared gets its pulse.
    // Running it for a tenant nobody is looking at would consume that diff silently.
    model = (scopeChosen && scoped.length) ? buildModel(scoped[0]) : null;

    root.className = 'content-body topo-page';
    root.innerHTML = barHtml() +
      (scopeChosen && !composing && state.view === 'ngfw' ? deckBarHtml() : '') +
      `<div class="topo-canvas ${state.flow ? '' : 'no-flow'}" id="topoCanvas">
        <div class="topo-viewport" id="topoViewport">
          <div class="topo-decks" id="topoDecks">
            <section class="topo-deck ${state.view === 'fabric' ? 'is-active' : ''}"
                     id="deck-fabric">${composing ? composingHtml() : canvasHtml(model)}</section>
            <section class="topo-deck ${state.view === 'ngfw' ? 'is-active' : ''}"
                     id="deck-ngfw">${scopeChosen ? (composing ? composingHtml() : ngfwDeck()) : ''}</section>
          </div>
        </div>
      </div>` +
      (scopeChosen && !composing && state.view === 'fabric' ? deckBarHtml() : '') +
      `<aside class="topo-drawer" id="topoDrawer"><div class="topo-drawer-h">
          <span class="topo-drawer-t" id="topoDrawerT">Node</span>
          <button class="topo-drawer-x" id="topoDrawerX" type="button">&times;</button>
        </div><div class="topo-drawer-b" id="topoDrawerB"></div></aside>`;

    wire();
    requestAnimationFrame(() => { syncDeck(false); drawLinks(); drawNgfwLinks(); });
  }

  // Both decks are laid out at all times, stacked; the viewport shows one and slides to the other.
  // Sliding a transform (rather than swapping the DOM) is what makes the two feel like one screen
  // with an upstairs and a downstairs.
  function syncDeck(animate) {
    const decks = document.getElementById('topoDecks');
    if (!decks) return;

    const order = ['fabric', 'ngfw'];
    const i = Math.max(0, order.indexOf(state.view));

    decks.querySelectorAll('.topo-deck').forEach((el, k) => {
      const on = k === i;
      el.classList.toggle('is-active', on);
      el.setAttribute('aria-hidden', on ? 'false' : 'true');
    });

    // Each deck is exactly one viewport tall and scrolls its own content, so the step is a flat
    // -100% per deck — no measuring, and a deck growing or shrinking can never break the geometry.
    decks.style.transition = animate ? '' : 'none';
    decks.style.transform = `translateY(${-i * 100}%)`;
    if (!animate) {
      void decks.offsetHeight;   // settle the layout so the suppressed transition cannot leak
      decks.style.transition = '';
    }
  }

  function goDeck(view) {
    if (state.view === view) return;
    state.view = view;
    syncDeck(true);

    const btn = document.getElementById('deckSwitch');
    if (btn) btn.outerHTML = deckBtn(view === 'fabric' ? 'ngfw' : 'fabric');
    document.getElementById('deckSwitch')?.addEventListener('click', () =>
      goDeck(state.view === 'fabric' ? 'ngfw' : 'fabric'));
    placeDeckBar();

    // The cards assemble on arrival, not on every poll — a stagger that replayed every 30 seconds
    // would read as the page glitching rather than as the estate resolving into view.
    const deck = document.getElementById('deck-' + view);
    if (deck) {
      deck.classList.add('is-entering');
      setTimeout(() => deck.classList.remove('is-entering'), 900);
    }

    // Links are measured from laid-out cards, and an inactive deck is scaled — so they are only
    // worth redrawing once the fabric deck is the one on screen and settled.
    setTimeout(view === 'fabric' ? drawLinks : drawNgfwLinks, 560);
  }

  // ── Links + packets ─────────────────────────────────────────────────────────
  // Anchors are read from the laid-out DOM rather than computed from a virtual layout: the cards are
  // ordinary flow content, so the browser is the only thing that knows where they ended up.
  function edgeSpecs(m) {
    const out = [];

    shownZones(m).forEach((z) => {
      const gwGroup = z.groups.findIndex(g => g.svc === 'gp_gateway' && !g.absent);
      const swgGroup = z.groups.findIndex(g => g.svc === 'swg_proxy' && !g.absent);

      z.groups.forEach((g, gi) => {
        const gid = 'svc-' + z.zi + '-' + gi;
        if (!g.nodes.length && !g.lbs.length) return;

        if (g.spec.flow === 'rn') {
          out.push({ from: 'end-rn', to: gid, kind: 'rn', zone: z.name });
        } else if (g.spec.flow === 'swg') {
          // Direct proxy sessions (split tunnel / proxy mode) come straight from the two browser rows.
          out.push({ from: 'ep-pab', to: gid, kind: 'swg', zone: z.name });
          out.push({ from: 'ep-pac', to: gid, kind: 'swg', zone: z.name });
        } else if (g.spec.flow === 'mu') {
          out.push({ from: 'ep-gp', to: gid, kind: 'mu', zone: z.name });
        }

        g.lbs.forEach((lb) => g.nodes.forEach((n) => {
          out.push({ from: 'node:' + lb.key, to: 'node:' + n.key, kind: 'internal', zone: z.name, short: true });
        }));

        if (g.spec.egress && g.nodes.length) out.push({ from: gid, to: 'dst-net', kind: 'egress', zone: z.name });
      });

      // The full-tunnel hop: L4 gateway → L7 proxy, both inside Prisma Access. Drawn only where the
      // region actually has both, because that is the only place it can happen.
      if (gwGroup >= 0 && swgGroup >= 0) {
        out.push({ from: 'svc-' + z.zi + '-' + gwGroup, to: 'svc-' + z.zi + '-' + swgGroup,
                   kind: 'chain', zone: z.name, short: true,
                   label: z.zi === 0 ? 'full tunnel → SWG' : '' });
      }

    });

    return out.concat(coarseEdges(m));
  }

  // The links that belong to the fabric as a whole rather than to one region. One line per region
  // would have to travel down across every card below it to reach the private hand-off — and the
  // relationship really is "the fabric reaches these", not "this particular region does".
  function coarseEdges(m) {
    const out = [];
    // A Service Connection whose peer address is an address on a firewall we manage is a tunnel
    // configured end to end — see linkServiceConnections() in topologyd. That is the first thing on
    // the private side of this drawing the page has ever been able to state rather than reserve, so
    // it is the first line there allowed to carry packets: `sc` is drawn solid and animated, where
    // `pending` is the dashed lane that means "we cannot see whether anything is here".
    //
    // Deliberately not extended past the firewall. What sits behind it is not collected, and a
    // packet crossing into the applications card would be inventing the one hop nothing reports.
    const scBuilt = (((m.tenant || {}).sc || {}).connections || []).some(c => c.linked);

    if (m.dataZones.length) {
      // No coarse RN line when nothing is onboarded: a line into the fabric would imply an RN-SPN
      // that does not exist. When one does exist it is drawn per region, above.

      out.push({ from: 'data-plane', to: 'dst-sc', kind: scBuilt ? 'sc' : 'pending', zone: 'all' });
      out.push({ from: 'data-plane', to: 'dst-ztna', kind: 'pending', zone: 'all' });
    }

    // The portal is what the GlobalProtect app talks to before it has a gateway at all.
    if (m.ctlZones.length) out.push({ from: 'ep-gp', to: 'ctl-strip', kind: 'ctl', zone: 'all' });

    // The private estate is now one vertical stack under the hand-offs that reach it, so these are
    // plain top-to-bottom hops between neighbours and need no routing at all — geometry() draws a
    // stacked pair from the bottom edge to the top edge on its own.
    // Not `short`. That flag caps a line at one packet, which is right for a hop between two cards
    // sitting on top of each other — an NLB to its node — but this one spans the width of the stack
    // and a single dot on it read as a stray mark rather than as traffic. It gets the standard count
    // for its length, the same as the line feeding it from the fabric above.
    out.push({ from: 'dst-sc', to: 'dst-fw', kind: scBuilt ? 'sc' : 'pending', zone: 'all' });
    out.push({ from: 'dst-fw', to: 'dst-apps', kind: 'pending', zone: 'all', short: true });

    // No connector-to-firewall link, deliberately. A connector dials OUT to the fabric from a host
    // inside the estate; the firewall it happens to sit behind is not a peer and no tunnel is built
    // to it. Drawing one would put the connector on the Service Connection's footing, which is the
    // single thing about this half of the picture worth getting right.
    //
    // So the connector has exactly one link, and it goes straight to the applications — down the
    // outside of the stack and in from the far side. Drawn as a drop through the middle it would
    // read as passing through the firewall; the detour IS the statement.
    out.push({ from: 'dst-ztna', to: 'dst-apps', kind: 'pending', zone: 'all', enter: 'right' });
    return out;
  }

  function elFor(ref) {
    if (ref.indexOf('node:') === 0) {
      return document.querySelector('.topo-node[data-node="' + cssEscape(ref.slice(5)) + '"]');
    }
    return document.getElementById(ref);
  }

  const cssEscape = (s) => String(s).replace(/["\\]/g, '\\$&');

  function roundedPath(pts, radius) {
    let d = `M ${pts[0].x.toFixed(1)} ${pts[0].y.toFixed(1)}`;
    for (let i = 1; i < pts.length - 1; i++) {
      const p = pts[i], a = pts[i - 1], b = pts[i + 1];
      const la = Math.hypot(p.x - a.x, p.y - a.y) || 1;
      const lb = Math.hypot(b.x - p.x, b.y - p.y) || 1;
      const r = Math.min(radius, la / 2, lb / 2);
      const s1 = { x: p.x + (a.x - p.x) / la * r, y: p.y + (a.y - p.y) / la * r };
      const s2 = { x: p.x + (b.x - p.x) / lb * r, y: p.y + (b.y - p.y) / lb * r };
      d += ` L ${s1.x.toFixed(1)} ${s1.y.toFixed(1)}` +
           ` Q ${p.x.toFixed(1)} ${p.y.toFixed(1)} ${s2.x.toFixed(1)} ${s2.y.toFixed(1)}`;
    }
    const last = pts[pts.length - 1];
    return d + ` L ${last.x.toFixed(1)} ${last.y.toFixed(1)}`;
  }

  function drawLinks() {
    if (state.view !== 'fabric') return;   // the deck is scaled away; its rects would lie
    const stage = document.getElementById('topoStage');
    const svg = document.getElementById('topoLinks');
    if (!stage || !svg || !model) return;

    const rectOf = rectReader(stage);
    const drawn = [];
    edgeSpecs(model).forEach((sp) => {
      const a = elFor(sp.from), b = elFor(sp.to);
      if (!a || !b) return;
      drawn.push({ spec: sp, d: geometry(sp, rectOf(a), rectOf(b)),
                   dim: !(state.region === 'all' || sp.zone === 'all' || sp.zone === state.region) });
    });
    paintInto(svg, drawn);
  }

  // Rects relative to a stage, which is what every path here is expressed in. Anchors are read from
  // the laid-out DOM rather than computed from a virtual layout: the cards are ordinary flow content,
  // so the browser is the only thing that knows where they ended up.
  function rectReader(stage) {
    const origin = stage.getBoundingClientRect();
    return (el) => {
      const r = el.getBoundingClientRect();
      return { l: r.left - origin.left, r: r.right - origin.left, t: r.top - origin.top,
               b: r.bottom - origin.top, cx: r.left + r.width / 2 - origin.left,
               cy: r.top + r.height / 2 - origin.top };
    };
  }

  // Draw a computed link set into an SVG layer, packets and all. Shared by both decks: what a link
  // looks like and how a packet rides it is the page's visual language, not one deck's, and having
  // the NGFW deck grow its own copy would be how the two drift apart.
  function paintInto(svg, drawn) {
    // A second full rebuild only if the set of links actually changed. A sidebar sliding open fires a
    // resize on every frame; tearing the SVG down each time would restart every packet mid-flight
    // and leave the dots stuttering at the start of their paths. Instead the existing lines are
    // re-pointed, and each packet's motion path with them.
    const existing = svg.querySelectorAll('path.topo-link');
    if (existing.length === drawn.length) {
      drawn.forEach((x, i) => {
        existing[i].setAttribute('d', x.d);
        existing[i].setAttribute('class', 'topo-link kind-' + x.spec.kind + (x.dim ? ' is-dimmed' : ''));
        svg.querySelectorAll(`circle.topo-dot[data-fp="${i}"]`).forEach((dot) => {
          dot.style.offsetPath = `path('${x.d}')`;
          dot.setAttribute('class', 'topo-dot kind-' + x.spec.kind + (x.dim ? ' is-dimmed' : ''));
        });
      });
      svg.querySelectorAll('text.topo-link-label').forEach(t => t.remove());
      drawn.forEach((x, i) => {
        if (x.spec.kind === 'pending' && x.spec.label) label(svg, existing[i], x.spec.label, x.dim);
      });
      return;
    }

    svg.innerHTML = '';
    const paths = [];
    drawn.forEach((x, i) => {
      const path = document.createElementNS(NS, 'path');
      path.setAttribute('id', svg.id + '-fp-' + i);
      path.setAttribute('d', x.d);
      path.setAttribute('class', 'topo-link kind-' + x.spec.kind + (x.dim ? ' is-dimmed' : ''));
      svg.appendChild(path);
      paths.push({ path, spec: x.spec, dim: x.dim });
    });

    // The packets need each path's length, which only exists once it is in the document.
    paths.forEach(({ path, spec, dim }, i) => {
      if (spec.kind === 'pending' && spec.label) label(svg, path, spec.label, dim);
      if (spec.kind === 'pending') return;   // a lane with no data carries no packets

      const len = path.getTotalLength();
      if (!len) return;
      const dur = Math.max(0.9, len / PX_PER_SEC);
      const count = spec.short ? 1 : Math.max(2, Math.round(len / 150));
      const d = path.getAttribute('d');
      for (let k = 0; k < count; k++) {
        // The packet rides a CSS motion path rather than SMIL. animateMotion samples the path it
        // references when the animation starts and never re-reads it, so a link that moved left its
        // packets flying along the old geometry. offset-path is re-read whenever it is set, and the
        // animation drives offset-distance — so re-pointing it moves the packets without restarting
        // them mid-flight.
        const dot = document.createElementNS(NS, 'circle');
        dot.setAttribute('class', 'topo-dot kind-' + spec.kind + (dim ? ' is-dimmed' : ''));
        dot.setAttribute('r', '2.5');   // the class refines it; this keeps the dot visible regardless
        dot.setAttribute('cx', '0');
        dot.setAttribute('cy', '0');    // the motion path places it; the circle sits at the origin
        dot.dataset.fp = String(i);
        dot.style.offsetPath = `path('${d}')`;
        dot.style.animationDuration = dur.toFixed(2) + 's';
        dot.style.animationDelay = (-(dur / count) * k).toFixed(2) + 's';
        svg.appendChild(dot);
      }
    });
  }

  // ── NGFW deck edges ─────────────────────────────────────────────────────────
  // One line per relationship the firewall states about itself. Fabric links are grouped to the
  // tenant card rather than drawn per Service Connection: three connections into one tenant is one
  // relationship drawn three times, and on a twenty-firewall estate that is the difference between a
  // diagram and a ball of string. The per-connection detail is in the destination card and the drawer.
  function ngfwEdgeSpecs() {
    const out = [];
    const devs = ngfwForSite();
    const haveGp = devs.some(d => ((d.gp || {}).portals || []).length || ((d.gp || {}).gateways || []).length);

    devs.forEach((d) => {
      const my = linksOf(d.oid);
      const fw = 'nfw-' + d.oid;

      // Remote users arrive on the box, so the line runs into it, not through it.
      const gp = d.gp || {};
      if (haveGp && ((gp.portals || []).length || (gp.gateways || []).length))
        out.push({ from: 'ngp', to: fw, kind: 'mu', zone: 'all' });

      const tenants = new Set();
      let external = false;
      my.forEach((l) => {
        if (isFabric(l)) tenants.add(l.tenant || 'unknown');
        else external = true;
      });

      tenants.forEach((t) => {
        // Service Connections and Remote Networks are different lanes on the fabric deck and stay
        // different here, so the two decks read the same. A tenant reached by both takes the SC
        // colour, which is the stronger statement about the site.
        const kinds = my.filter(l => isFabric(l) && (l.tenant || 'unknown') === t).map(l => l.kind);
        out.push({ from: fw, to: 'ndest-' + t, zone: 'all',
                   kind: kinds.indexOf('service_connection') >= 0 ? 'sc' : 'rn' });
      });

      if (external) out.push({ from: fw, to: 'ndest-external', kind: 'egress', zone: 'all' });
    });

    return out;
  }

  function drawNgfwLinks() {
    if (state.view !== 'ngfw') return;
    const stage = document.getElementById('ngfwStage');
    const svg = document.getElementById('ngfwLinks');
    if (!stage || !svg) return;

    const rectOf = rectReader(stage);
    const drawn = [];
    ngfwEdgeSpecs().forEach((sp) => {
      const a = document.getElementById(sp.from), b = document.getElementById(sp.to);
      if (!a || !b) return;
      drawn.push({ spec: sp, d: geometry(sp, rectOf(a), rectOf(b)), dim: false });
    });
    paintInto(svg, drawn);
  }

  // Where one link runs, given the two boxes it joins. Pure geometry: no DOM, so it can be re-run on
  // every frame of a resize without touching the drawing.
  function geometry(s, ra, rb) {
    // Round the outside and in from the target's right edge. For a source that stands beside the
    // stack rather than above it, this is the only path that does not cross what it is bypassing:
    // the vertical run stays clear of the column, so the line never touches the cards it skips.
    if (s.enter === 'right') {
      // Out the SIDE, down the outside, and back in the target's far edge — three straight runs and
      // two corners, none of them over a card. Leaving downwards instead put the first corner
      // directly under the source and dropped the line past the edge of every card in the stack,
      // which read as a line squeezing between them rather than as one going around. Out the right
      // edge it never enters the column at all, and the detour — the whole statement this link
      // makes, that a connector reaches the applications without passing the firewall — is what the
      // eye follows.
      const chanX = Math.max(ra.r, rb.r) + 18;
      return roundedPath([{ x: ra.r, y: ra.cy }, { x: chanX, y: ra.cy },
                          { x: chanX, y: rb.cy }, { x: rb.r, y: rb.cy }], 12);
    }

    if (s.route !== undefined) {
      // Straight across would cut through the card standing between the two columns, so the link
      // leaves downwards into the margin under the row, runs along it, climbs the empty channel
      // between the columns and comes in from the side. One lane and one channel per link.
      const i = s.route;
      const laneY = ra.b + 16 + i * 9;
      const chanX = rb.l - 9 - i * 4;   // hugs the target column: open space to the left of it
      return roundedPath([{ x: ra.cx, y: ra.b }, { x: ra.cx, y: laneY }, { x: chanX, y: laneY },
                          { x: chanX, y: rb.cy }, { x: rb.l, y: rb.cy }], 14);
    }

    // Side by side → leave the right edge and arrive at the left. Stacked (same column, or one
    // service group above another inside a region) → leave the bottom and arrive at the top.
    if (rb.l < ra.r - 4) {
      const p1 = { x: ra.cx, y: ra.b }, p2 = { x: rb.cx, y: rb.t };
      const dy = Math.max(10, Math.min(60, Math.abs(p2.y - p1.y) * 0.55));
      return `M ${p1.x.toFixed(1)} ${p1.y.toFixed(1)} C ${p1.x.toFixed(1)} ${(p1.y + dy).toFixed(1)}, ` +
             `${p2.x.toFixed(1)} ${(p2.y - dy).toFixed(1)}, ${p2.x.toFixed(1)} ${p2.y.toFixed(1)}`;
    }

    const p1 = { x: ra.r, y: ra.cy }, p2 = { x: rb.l, y: rb.cy };
    const dx = Math.max(26, Math.min(150, Math.abs(p2.x - p1.x) * 0.45));
    return `M ${p1.x.toFixed(1)} ${p1.y.toFixed(1)} C ${(p1.x + dx).toFixed(1)} ${p1.y.toFixed(1)}, ` +
           `${(p2.x - dx).toFixed(1)} ${p2.y.toFixed(1)}, ${p2.x.toFixed(1)} ${p2.y.toFixed(1)}`;
  }

  function label(svg, path, text, dim, at) {
    const len = path.getTotalLength();
    if (!len) return;
    const p = path.getPointAtLength(len * (at || 0.42));
    const t = document.createElementNS(NS, 'text');
    t.setAttribute('class', 'topo-link-label' + (dim ? ' is-dimmed' : ''));
    t.setAttribute('x', p.x.toFixed(1));
    t.setAttribute('y', (p.y - 6).toFixed(1));
    t.setAttribute('text-anchor', 'middle');
    t.textContent = text;
    svg.appendChild(t);
  }

  // ── Detail drawer ───────────────────────────────────────────────────────────
  // Every opener ends here: set the title, set the body, slide it in. Kept in one place so the
  // selection bookkeeping cannot drift between the dozen cards that now open it.
  function drawer(title, html) {
    state.selected = null;
    document.getElementById('topoDrawerT').textContent = title;
    document.getElementById('topoDrawerB').innerHTML = html;
    document.getElementById('topoDrawer').classList.add('open');
    document.querySelectorAll('.topo-node.selected').forEach(el => el.classList.remove('selected'));
  }

  // The two shapes every detail body is built from.
  const kvRow = (k, v, mono) => (v === '' || v === undefined || v === null)
    ? '' : `<dt>${esc(k)}</dt><dd class="${mono ? 'mono' : ''}">${esc(v)}</dd>`;
  const kv = (rows) => rows.filter(Boolean).length ? `<dl class="topo-kv">${rows.join('')}</dl>` : '';
  const sec = (t) => `<div class="topo-drawer-sec">${esc(t)}</div>`;
  const hint = (t) => `<p class="field-hint">${t}</p>`;

  function findNode(key) {
    if (!model) return null;
    for (const z of model.zones)
      for (const g of z.groups)
        for (const n of g.nodes.concat(g.lbs, g.aux))
          if (n.key === key) return { node: n, zone: z, group: g };
    return null;
  }

  function openDrawer(key) {
    const hit = findNode(key);
    if (!hit) return;
    const { node: n, zone: z, group: g } = hit;
    state.selected = key;

    const row = kvRow;

    drawer(n.address, `
      <dl class="topo-kv">
        ${row('Region', z.name)}
        ${row('Service', g.spec.label)}
        ${row('Role', ADDR_LABEL[n.addressType] || n.addressType)}
        ${row('Address', n.address, true)}
        ${row('Created', n.created ? window.NMS.utils.fmtTs(n.created * 1000) + ' (' + relAge(n.age) + ' ago)' : '')}
        ${n.allowListed === undefined ? '' : row('Allow-listed', n.allowListed ? 'yes' : 'no')}
        ${n.addressType !== 'network_load_balancer' ? '' : row('NLB active', n.lbActive === undefined
            ? 'not reported by the tenant' + (n.regionalFqdn ? ' — published as a regional entry point' : '')
            : (n.lbActive ? 'yes' : 'no — address allocated, IP Optimization not carrying traffic here'))}
        ${row('Regional FQDN', n.regionalFqdn, true)}
        ${row('Geo LB FQDN', z.geoFqdn, true)}
        ${row('Geo LB CNAME', z.geoCname, true)}
      </dl>
      ${z.subnets.length ? `<div class="topo-drawer-sec">Region subnets (v4)</div>
        <div class="topo-sub-list">${z.subnets.map(s => `<span class="topo-sub">${esc(s)}</span>`).join('')}</div>` : ''}
      ${z.subnets6.length ? `<div class="topo-drawer-sec">Region subnets (v6)</div>
        <div class="topo-sub-list">${z.subnets6.map(s => `<span class="topo-sub">${esc(s)}</span>`).join('')}</div>` : ''}`);

    state.selected = key;
    const el = document.querySelector('.topo-node[data-node="' + cssEscape(key) + '"]');
    if (el) el.classList.add('selected');
  }

  // The private-side firewalls are ours, so the drawer says what we actually know about one — and,
  // just as importantly, what we do not: which Service Connection reaches it.
  function openFwDrawer(oid) {
    const d = state.ngfw.find(x => x.oid === oid);
    if (!d) return;
    const row = kvRow;
    const word = d.status === 'active' ? 'reachable' : d.status === 'down' ? 'unreachable' : 'not probed yet';
    const peers = linksOf(d.oid);

    drawer(d.name || d.target || 'firewall', `
      <dl class="topo-kv">
        ${row('Role', 'On-premise NGFW')}
        ${row('Site', d.site_name)}
        ${row('Management address', d.target, true)}
        ${row('Status', word)}
      </dl>
      ${peers.length ? sec('Configured peers') + `<div class="topo-dl">${peers.map(l =>
          `<div class="topo-dl-r"><span class="topo-dl-k k-${esc(l.kind)}">${
            l.kind === 'service_connection' ? 'SC' : l.kind === 'remote_network' ? 'RN' : 'ext'}</span>
             <span class="topo-dl-n">${esc(l.label || l.peer)}</span>
             <span class="topo-dl-v mono">${esc(l.peer)}</span></div>`).join('')}</div>` : ''}
      ${fwDetail(d)}
      ${sec('Why it is in this picture')}
      ${hint(`A Service Connection reaches this box when its IKE gateway's peer address is one of the
              addresses above — the Service Connection card does that match. A ZTNA connector never
              does: it dials out from a host inside the estate and this firewall is not its peer.`)}`);
  }

  // ── One panel per card kind ─────────────────────────────────────────────────
  // The drawer is where a card stops summarising. Each of these answers the question its card
  // raises and says plainly where the answer came from — or that no API reports it, which on this
  // page is a real answer and not a gap to paper over.
  function openDetail(kind, key) {
    const m = model;
    const t = m && m.tenant;

    if (kind === 'node') return openDrawer(key);
    if (kind === 'fw')   return openFwDrawer(key);

    if (kind === 'mu') {
      const c = m ? m.counts : { gw: 0, swg: 0, portal: 0 };
      return drawer('Mobile Users', `
        ${kv([kvRow('Endpoint kinds', 'GlobalProtect app · Prisma Access Browser · Browser + PAC'),
              kvRow('GP gateway addresses', c.gw), kvRow('Explicit Proxy addresses', c.swg),
              kvRow('GP portal addresses', c.portal)])}
        ${sec('How a session gets in')}
        ${hint(`The GlobalProtect app talks to the <b>portal</b> first, takes the gateway list it
                answers with, and picks one itself — which is why a region's gateway NLB can be
                allocated and still carry nothing. A browser instead resolves one name,
                <code>…proxy.prismaaccess.com</code>; DNS steers it to a region and that region's NLB
                spreads it over the proxy nodes. Client-side choice on one lane, server-side on the
                other.`)}
        ${sec('The two chained')}
        ${hint(`In full tunnel a browser session rides the L4 tunnel first and is proxied by the SWG
                behind it — GP gateway → Explicit Proxy → internet, both hops inside Prisma Access.
                Under split tunnel it reaches the SWG directly. DNS resolves from wherever the tunnel
                puts it, so full tunnel also decides which proxy region the browser lands in.`)}`);
    }

    if (kind === 'rn') {
      const zones = m ? m.dataZones.filter(z => z.groups.some(g => g.svc === 'remote_network' && !g.absent)) : [];
      return drawer('Remote Users', `
        ${kv([kvRow('RN-SPN addresses', (m && m.counts.rn) || 0),
              kvRow('Regions onboarded', zones.length)])}
        ${zones.length
          ? sec('Where branch tunnels land') + zones.map(z => {
              const g = z.groups.find(x => x.svc === 'remote_network' && !x.absent);
              return `<div class="topo-dl"><div class="topo-dl-r">
                  <span class="topo-dl-n">${esc(z.name)}</span></div>${
                g.nodes.map(n => `<div class="topo-dl-r"><span class="topo-dl-k">${
                  esc(ADDR_LABEL[n.addressType] || n.addressType)}</span>
                  <span class="topo-dl-v mono">${esc(n.address)}</span></div>`).join('')}</div>`;
            }).join('')
          : sec('Not configured') + hint(`No remote network is onboarded in this tenant. This same API
              reports <code>serviceType: remote_network</code> for every region a branch is onboarded
              to, so an empty answer here is the tenant's answer, not a missing read.`)}
        ${sec('Not readable from this API')}
        ${hint('BGP routes and per-branch bandwidth need the remote-network read, which is not collected.')}`);
    }

    if (kind === 'ctl') {
      const zones = m ? m.ctlZones : [];
      return drawer('Control plane & shared services', `
        ${kv([kvRow('Locations', zones.length),
              kvRow('Addresses', zones.reduce((a, z) => a + z.groups.reduce((b, g) =>
                b + g.nodes.length + g.aux.length, 0), 0))])}
        ${zones.map(z => sec(z.name) + `<div class="topo-dl">${z.groups.map(g =>
            g.nodes.concat(g.aux).map(n => `<div class="topo-dl-r">
              <span class="topo-dl-k">${esc(ADDR_LABEL[n.addressType] || n.addressType)}</span>
              <span class="topo-dl-v mono">${esc(n.address)}</span></div>`).join('')).join('')}</div>`).join('')}
        ${sec('Why they are not in the data plane')}
        ${hint(`A portal hands the app its configuration and its gateway list; the auth cache is
                tenant-global. Neither carries a user's traffic, so neither is drawn as a lane.`)}`);
    }

    // The frame owns every region and every lane inside it, so its panel is the whole data plane:
    // each region, each of its three slots, and every address under them. The lanes and the regions
    // carry no mark of their own — a mark on a card inside a card teaches the reader to hunt for
    // which level of the drawing is the clickable one.
    if (kind === 'plane') {
      const zones = m ? m.dataZones : [];
      const c = m ? m.counts : { gw: 0, swg: 0, rn: 0, lb: 0 };
      const addrs = (g) => g.lbs.concat(g.nodes, g.aux).map(n => `<div class="topo-dl-r">
          <span class="topo-dl-k">${esc(ADDR_LABEL[n.addressType] || n.addressType)}</span>
          <span class="topo-dl-v mono">${esc(n.address)}</span>
          <span class="topo-dl-t">${esc(relAge(n.age))}</span></div>`).join('');

      return drawer('Data plane · traffic regions', `
        ${kv([kvRow('Traffic regions', zones.length),
              kvRow('GlobalProtect gateway addresses', c.gw),
              kvRow('Explicit Proxy addresses', c.swg),
              kvRow('RN-SPN addresses', c.rn),
              kvRow('Load balancers', c.lb)])}
        ${zones.map(z => sec(z.name) + kv([
            kvRow('Kind', z.dataplane ? 'traffic region' : 'portal region'),
            kvRow('Proxy geo-LB', z.geoFqdn, true),
            kvRow('Geo-LB CNAME', z.geoCname, true),
            kvRow('Regional FQDN', (z.groups.reduce((a, g) => a.concat(g.lbs), [])
                                     .find(n => n.regionalFqdn) || {}).regionalFqdn, true),
          ]) + z.groups.map(g => `<div class="topo-dl">
              <div class="topo-dl-r"><span class="topo-dl-n">${esc(g.spec.label)}</span>
                ${g.absent ? '<span class="topo-dl-t">not configured</span>'
                           : `<span class="topo-dl-t">${g.nodes.length + g.aux.length} node${
                               g.nodes.length + g.aux.length === 1 ? '' : 's'}</span>`}</div>
              ${g.absent ? '' : addrs(g)}
            </div>`).join('') + (z.subnets.length
              ? `<div class="topo-sub-list">${z.subnets.map(x =>
                  `<span class="topo-sub mono">${esc(x)}</span>`).join('')}</div>` : '')).join('')}
        ${sec('What the addresses are')}
        ${hint(`Sessions arrive on a region's <b>load balancer</b>. The addresses listed under it are
                <b>egress</b> — the source IPs the internet sees, and what a SaaS allow-list carries.
                A lane marked not configured is the tenant's answer, not a missing read.`)}`);
    }

    if (kind === 'net') {
      const ips = m ? m.counts.egressIps : [];
      const byZone = new Map();
      ips.forEach(x => { if (!byZone.has(x.zone)) byZone.set(x.zone, []); byZone.get(x.zone).push(x.address); });
      return drawer('Internet & SaaS', `
        ${kv([kvRow('Egress addresses', ips.length), kvRow('Regions', byZone.size)])}
        ${[...byZone.entries()].map(([zn, list]) => sec(zn) +
          `<div class="topo-sub-list">${list.map(a => `<span class="topo-sub mono">${esc(a)}</span>`).join('')}</div>`).join('')}
        ${sec('What to do with them')}
        ${hint(`These are the source IPs a SaaS allow-list must carry. An allow-list is written per
                region as often as it is written whole, which is why each address says where it
                egresses from. They are outbound only — nothing connects to them.`)}`);
    }

    if (kind === 'sc') {
      const sc = (t && t.sc) || {};
      const list = Array.isArray(sc.connections) ? sc.connections : [];
      return drawer('Service Connection', `
        ${kv([kvRow('Connections', list.length),
              kvRow('Subnets published', list.reduce((n, c) => n + ((c.subnets || []).length), 0)),
              kvRow('Read', sc.collected_at ? relStamp(sc.collected_at) : '')])}
        ${list.map(c => sec(c.name || c.id || 'connection') + kv([
            kvRow('Region', c.region || c.region_tag), kvRow('IPsec tunnel', c.ipsec_tunnel, true),
            kvRow('IKE gateway', c.ike_gateway, true),
            kvRow('Peer address', c.peer_ip || c.peer_fqdn || (c.peer_dynamic ? 'dynamic' : ''), true),
            kvRow('State', c.linked
                    ? 'built end to end — peer is ' + (c.linked_device_name || 'a managed firewall') +
                      (c.linked_interface ? ' ' + c.linked_interface : '')
                    : c.peer_ip ? 'peer is not an address on any firewall in this scope'
                    : c.peer_fqdn ? 'peer is an FQDN — cannot be matched to an interface'
                    : c.peer_dynamic ? 'peer address is dynamic — nothing to match'
                    : 'no peer address resolved'),
            kvRow('Backup tunnel', c.backup_SC || c.secondary_ipsec_tunnel, true),
            kvRow('Source NAT', c.source_nat === undefined ? '' : (c.source_nat ? 'on' : 'off')),
            kvRow('BGP', ((c.protocol || {}).bgp || {}).enable || c.bgp_peer ? 'enabled' : 'not enabled'),
            kvRow('Onboarding', c.onboarding_type),
          ]) + ((c.subnets || []).length
            ? `<div class="topo-sub-list">${c.subnets.map(x => `<span class="topo-sub mono">${esc(x)}</span>`).join('')}</div>`
            : '')).join('')}
        ${list.length ? '' : sec('Not collected') + hint('No Service Connection has been read for this tenant.')}
        ${sec('How the peer is known')}
        ${hint(`The deployment read answers eight fields and a peer address is not among them. It
                names an <code>ipsec_tunnel</code>; that tunnel names an IKE gateway; that gateway
                records the address Prisma Access dials — which is your own firewall. topologyd walks
                those three documents and matches the result against every address configured on the
                firewalls in this scope.`)}
        ${sec('Built is not up')}
        ${hint(`A match means both ends name each other, so the tunnel is configured end to end and
                traffic <b>can</b> flow. It does not mean traffic <b>is</b> flowing: neither the
                deployment API nor the config APIs report tunnel state, IKE phase or a byte counter.
                Nothing collected here does, so nothing here draws an "up" light.`)}`);
    }

    if (kind === 'ztna') {
      const z = (t && t.ztna) || {};
      const conns = Array.isArray(z.connectors) ? z.connectors : [];
      const groups = Array.isArray(z.groups) ? z.groups : [];
      const names = z.group_names || {};
      const up = conns.filter(c => (c.flags || {}).tunnel_up && (c.flags || {}).control_plane_up).length;
      return drawer('ZTNA Connector', `
        ${kv([kvRow('Connectors', conns.length), kvRow('Groups', groups.length),
              kvRow('Healthy', conns.length ? up + ' / ' + conns.length : ''),
              kvRow('Read', z.collected_at ? relStamp(z.collected_at) : '')])}
        ${conns.length ? sec('Connectors') + `<div class="topo-dl">${conns.map(c => {
            const f = c.flags || {};
            const ok = f.tunnel_up && f.control_plane_up;
            return `<div class="topo-dl-r"><span class="topo-dot ${ok ? 'ok' : 'bad'}"></span>
                <span class="topo-dl-n">${esc(c.name || c.oid || 'connector')}</span>
                <span class="topo-dl-k">${esc(names[c.group] || '')}</span>
                <span class="topo-dl-v mono">${esc(c.cgnx_vion_ip || '')}</span>
                <span class="topo-dl-t">${esc(!f.tunnel_up ? 'tunnel down'
                  : !f.control_plane_up ? 'control plane down' : 'up')}</span></div>`;
          }).join('')}</div>`
          : sec('Not configured') + hint('No ZTNA connector is reported for this tenant.')}
        ${sec('Why it has no link to the firewall')}
        ${hint(`A connector VM dials OUT to a Zero Trust Tunnel termination point in the region. No
                routing from your network is involved, so overlapping app subnets are fine and the
                application is reached without crossing the firewall — which is why its line goes
                around the stack rather than through it.`)}`);
    }

    if (kind === 'fwcard') {
      const devs = ngfwForSite();
      return drawer('On-premise NGFW', `
        ${kv([kvRow('Firewalls in scope', devs.length),
              kvRow('Reachable', devs.filter(d => d.status === 'active').length)])}
        ${devs.map(d => {
          const peers = linksOf(d.oid);
          return sec(d.name || d.target || 'firewall') + kv([
            kvRow('Management address', d.target, true),
            kvRow('Status', d.status === 'active' ? 'reachable' : d.status === 'down' ? 'unreachable' : 'not probed yet'),
            kvRow('Site', d.site_name),
            kvRow('Fabric peers', peers.filter(isFabric).length),
          ]) + (peers.length ? `<div class="topo-dl">${peers.map(l =>
              `<div class="topo-dl-r"><span class="topo-dl-k k-${esc(l.kind)}">${
                l.kind === 'service_connection' ? 'SC' : l.kind === 'remote_network' ? 'RN' : 'ext'}</span>
                 <span class="topo-dl-n">${esc(l.label || l.peer)}</span>
                 <span class="topo-dl-v mono">${esc(l.peer)}</span></div>`).join('')}</div>` : '');
        }).join('') || hint('No NGFW is managed in this scope.')}
        ${sec('Peer, not management')}
        ${hint(`The peer chips are IKE gateway peer addresses — where the box dials Prisma Access. The
                address beside the name is how <em>we</em> reach the box. Which Service Connection
                reaches which firewall is now readable from the other direction: see the Service
                Connection card, which matches its peer address against these interfaces.`)}`);
    }

    if (kind === 'apps') {
      return drawer('Private apps & Data Center', `
        ${kv([kvRow('State', 'not configured')])}
        ${sec('Not readable from these APIs')}
        ${hint(`Applications, servers and segments the firewall fronts. What each Service Connection
                or ZTNA connector actually publishes is not reported by any endpoint collected here —
                the Service Connection read answers subnets, which is the nearest thing, and those
                are listed on that card instead.`)}`);
    }

    if (kind === 'gp') {
      const devs = ngfwForSite();
      const portals = [], gateways = [];
      devs.forEach(d => {
        ((d.gp || {}).portals || []).forEach(x => portals.push({ d, x }));
        ((d.gp || {}).gateways || []).forEach(x => gateways.push({ d, x }));
      });
      return drawer('GlobalProtect — on-premise', `
        ${kv([kvRow('Portals', portals.length), kvRow('Gateways', gateways.length),
              kvRow('Client pools', gateways.reduce((n, g) => n + ((g.x.pools || []).length), 0))])}
        ${portals.length ? sec('Portals') + `<div class="topo-dl">${portals.map(({ d, x }) =>
          `<div class="topo-dl-r"><span class="topo-dl-n">${esc(x.name)}</span>
             <span class="topo-dl-k">${esc(d.name || d.target)}</span>
             <span class="topo-dl-v mono">${esc(x.interface || '—')}</span></div>`).join('')}</div>` : ''}
        ${gateways.length ? sec('Gateways') + `<div class="topo-dl">${gateways.map(({ d, x }) =>
          `<div class="topo-dl-r"><span class="topo-dl-n">${esc(x.name)}</span>
             <span class="topo-dl-k">${esc(d.name || d.target)}</span>
             <span class="topo-dl-v mono">${esc((x.pools || []).join(', ') || x.interface || '—')}</span></div>`).join('')}</div>` : ''}
        ${sec('Not the same GlobalProtect')}
        ${hint(`These users terminate ON the firewall — they do not travel through Prisma Access to
                get there. The MU-SPN GlobalProtect lane on the fabric deck is the other one.`)}`);
    }

    if (kind === 'dest') {
      const g = destGroups().tenants.find(x => x.key === key);
      if (!g) return;
      const rows = (arr, k) => arr.map(l => `<div class="topo-dl-r">
          <span class="topo-dl-k k-${k}">${k.toUpperCase()}</span>
          <span class="topo-dl-n">${esc(l.label || l.peer)}</span>
          <span class="topo-dl-v mono">${esc(l.peer)}</span>
          <span class="topo-dl-t">${esc(l.region || '')}</span></div>`).join('');
      return drawer(g.key === 'unknown' ? 'Prisma Access tenant' : g.key, `
        ${kv([kvRow('Service Connections', g.sc.length), kvRow('Remote Networks', g.rn.length),
              kvRow('Firewalls reaching it', g.devices.size)])}
        ${g.sc.length ? sec('Service Connections') + `<div class="topo-dl">${rows(g.sc, 'sc')}</div>` : ''}
        ${g.rn.length ? sec('Remote Networks') + `<div class="topo-dl">${rows(g.rn, 'rn')}</div>` : ''}
        ${sec('Where these come from')}
        ${hint(`Every row is one IKE gateway's configured peer, read off the firewall itself — a fact
                the box states about what it reaches, not an inference from addresses that happen to
                look related.`)}`);
    }

    if (kind === 'ext') {
      const ext = destGroups().external;
      const byPeer = new Map();
      ext.forEach(l => { if (!byPeer.has(l.peer)) byPeer.set(l.peer, l); });
      return drawer('External peers', `
        ${kv([kvRow('Peers', byPeer.size), kvRow('Tunnels', ext.length)])}
        <div class="topo-dl">${[...byPeer.values()].map(l => `<div class="topo-dl-r">
            <span class="topo-dl-n">${esc(l.label || l.peer)}</span>
            <span class="topo-dl-v mono">${esc(l.peer)}</span>
            <span class="topo-dl-t">${esc(l.gateway || '')}</span></div>`).join('')}</div>
        ${sec('What makes a peer external')}
        ${hint(`It carries no tenant token, so topologyd's classifyPeer could not place it inside a
                Prisma Access fabric. A partner, a branch of your own, or a second vendor — the
                firewall names it as a peer and that is all this read knows.`)}`);
    }
  }

  function closeDrawer() {
    state.selected = null;
    const d = document.getElementById('topoDrawer');
    if (d) d.classList.remove('open');
    document.querySelectorAll('.topo-node.selected').forEach(el => el.classList.remove('selected'));
  }

  // ── Wiring ──────────────────────────────────────────────────────────────────
  function wire() {
    // The OS draws native option lists and CSS cannot reach them, so the app substitutes its own —
    // the same themed dropdown every Configuration select uses. The <select> stays as the value
    // store, so this listener is unaffected by the swap.
    const siteSel = document.getElementById('topoSite');
    if (siteSel) {
      // The scope now travels to topologyd, so changing it is a re-fetch rather than a re-filter.
      // The drawn estate belongs to the site being left, so it is cleared first — otherwise it
      // counts as "data we already have", the composing ring never starts, and the old site's
      // picture sits on screen until the new one happens to arrive.
      siteSel.addEventListener('change', (e) => {
        state.site = e.target.value;
        window.NMS.utils.siteScope.set(state.site);
        // Both decks stand down under "All sites" and the deck switch goes with them, so a viewer
        // left on the NGFW deck would be stranded on an empty one with no way back.
        if (!state.site) state.view = 'fabric';
        state.tenants = [];
        state.ngfw = [];
        state.sources = {};
        state.generatedAt = '';
        // The retry budget and any error belong to the site being left — a previous site that ran
        // out of tries must not make the next one give up on its first answer.
        pendingSince = 0;
        state.error = '';
        clearTimeout(pendingTimer);
        // Nothing on the "All sites" screen waits for data, so nothing should appear to.
        if (state.site) startComposing();
        render();
        load();
      });
      window.NMS.utils.enhanceSelect?.(siteSel);
    }

    document.getElementById('planeToggle')?.addEventListener('click', () => {
      state.planeOpen = !state.planeOpen;
      try { localStorage.setItem('topo.plane', state.planeOpen ? 'open' : 'closed'); } catch (_) { /* private mode */ }
      render();
    });

    const flow = document.getElementById('topoFlow');
    if (flow) flow.addEventListener('click', () => {
      state.flow = !state.flow;
      flow.classList.toggle('on', state.flow);
      document.getElementById('topoCanvas').classList.toggle('no-flow', !state.flow);
    });

    const live = document.getElementById('topoLive');
    if (live) live.addEventListener('click', () => { state.live = !state.live; live.classList.toggle('on', state.live); schedule(); });

    document.getElementById('deckSwitch')?.addEventListener('click', () =>
      goDeck(state.view === 'fabric' ? 'ngfw' : 'fabric'));

    const canvas = document.getElementById('topoCanvas');
    if (canvas) canvas.addEventListener('click', (e) => {
      const card = e.target.closest('[data-site]');
      if (card) {
        const sel = document.getElementById('topoSite');
        if (sel) { sel.value = card.dataset.site; sel.dispatchEvent(new Event('change', { bubbles: true })); }
        return;
      }

      // The one way into the drawer. It comes first: a detail icon sits inside cards that are
      // themselves clickable for other reasons, and the icon must win over what it stands on.
      const det = e.target.closest('[data-det]');
      if (det) { openDetail(det.dataset.det, det.dataset.detk); return; }

      // Node chips keep their whole-chip click. They are chips inside a lane, not cards, and an icon
      // on each would cost more room than the address it sits beside — the lane's own icon opens the
      // same addresses in a list.
      const n = e.target.closest('.topo-node[data-node]');
      if (n) { openDrawer(n.dataset.node); return; }

      // The NGFW deck's own controls. Density is a whole-deck decision, so it re-renders; expanding
      // one firewall's ports is not, so it swaps that one box in place — re-rendering the deck to
      // open a row would restage every other card and lose the scroll position.
      if (e.target.closest('#ngfwDensity')) {
        state.ngfwDense = !isDense();
        state.fwOpen = {};
        render();
        return;
      }

      const row = e.target.closest('[data-fwrow]');
      if (row) {
        const oid = row.dataset.fwrow;
        state.fwOpen[oid] = !state.fwOpen[oid];
        const box = ngfwForSite().find(d => d.oid === oid);
        if (box) {
          row.outerHTML = fwBox(box, isDense());
          // The box just changed height, so every link that lands on it moved with it.
          requestAnimationFrame(drawNgfwLinks);
        }
        return;
      }

      // Nothing above claimed the click, so it was aimed at the drawing rather than at anything in
      // it — which is how someone puts a panel away. Every opener returns before reaching here, so
      // moving between two details still swaps the panel rather than closing and reopening it. The
      // drawer is a sibling of the canvas, so a click inside the panel never arrives here at all.
      closeDrawer();
    });

    const x = document.getElementById('topoDrawerX');
    if (x) x.addEventListener('click', closeDrawer);
  }

  // ── Data ────────────────────────────────────────────────────────────────────
  async function load() {
    let settled = false;   // an answer arrived and the ring, if any, may run out

    try {
      // The site is asked for on the wire now, not filtered here: topologyd composes one site, so
      // sending the scope is what makes the answer small on a large estate. `site=` (empty) means
      // every site, which is the same contract the page had before.
      const r = await fetch('/api/topology?site=' + encodeURIComponent(state.site),
                            { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();

      // Whatever came back is drawn — it is the newest thing that exists. `pending` says a fresher
      // composition is on its way, which is a reason to come back in a moment, NOT a reason to throw
      // away the picture: a round trip is ~20ms and blanking the page for it would be a flicker.
      if (d.sase || d.ngfw) {
        state.tenants = (d.sase && Array.isArray(d.sase.tenants)) ? d.sase.tenants : [];
        state.ngfw = (d.ngfw && Array.isArray(d.ngfw.devices)) ? d.ngfw.devices : [];
        // The off-box relationships, and which end of them is the hub. Both are composed by
        // topologyd from the IKE gateway document — the page draws them, it does not derive them.
        state.links = (d.ngfw && Array.isArray(d.ngfw.links)) ? d.ngfw.links : [];
        state.shape = (d.ngfw && d.ngfw.shape) || {};
        state.siteList = Array.isArray(d.sites) ? d.sites : [];
        state.sources = d.sources || {};
      }
      state.generatedAt = d.generated_at || '';
      state.pending = !!d.pending;
      state.error = '';

      // A remembered site can have been deleted between visits, and a first visit to a one-site
      // estate has exactly one sensible answer. Both settled here, on the first real answer only —
      // `pending` is not an answer, it is mgmtd saying it is still asking.
      if (state.booting && !d.pending) {
        state.booting = false;
        const known = (state.siteList || []).map(x => x.oid);
        if (state.site && known.indexOf(state.site) === -1) {
          state.site = '';
          window.NMS.utils.siteScope.set('');
          return load();
        }
        if (!state.site && known.length === 1 && !window.NMS.utils.siteScope.get()) {
          state.site = known[0];
          window.NMS.utils.siteScope.set(state.site);
          return load();
        }
      }

      const haveData = !!(state.tenants.length || state.ngfw.length);

      if (state.pending) {
        // Nothing to draw yet, so the ring takes the deck until an answer lands. mgmtd answers
        // `pending` while topologyd composes; the retry is at conversation speed rather than the
        // next Live tick, because waiting a minute for 20ms of work looks like a broken page.
        if (!haveData && state.site && !state.booting) startComposing();

        if (!pendingSince) pendingSince = Date.now();

        if (Date.now() - pendingSince < COMPOSE_TIMEOUT_MS) {
          clearTimeout(pendingTimer);
          pendingTimer = setTimeout(load, PENDING_RETRY_MS);
        } else {
          // topologyd is not answering. Say so rather than spinning forever — and keep drawing what
          // we last knew, because a last-known picture still beats an empty one. `answered` stays
          // false, so the ring never completed and did not claim otherwise.
          state.pending = false;
          state.error = 'topologyd did not answer';
          pendingSince = 0;
          settled = true;
        }
      } else {
        pendingSince = 0;
        state.answered = true;   // lets the ring run to 100% on its way out
        settled = true;
      }

      // The on-premise deck judges a firewall the way the Home page does — three dependency layers,
      // not just "did it answer a ping".
      try {
        const lr = await fetch('/api/status/devices', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
        if (lr.ok) state.layers = (await lr.json()) || {};
      } catch (_) { /* keep the last snapshot */ }
    } catch (e) {
      state.error = e.message || 'load failed';
      state.booting = false;
      settled = true;
    }

    // A ring that is up owns the deck until it runs out, so the draw waits for it.
    const draw = () => { render(); if (state.selected) openDrawer(state.selected); };

    if (settled && state.composing) {
      finishComposing(draw);
    } else if (state.composing) {
      // Still waiting. Draw once to put the ring on screen, then leave it alone — the retries fire
      // every 400ms and re-rendering would rebuild the ring's markup underneath it, snapping the
      // arc back to zero several times a second.
      if (!document.querySelector('.topo-ring .tr-fill')) draw();
    } else {
      draw();
    }
  }

  function schedule() {
    clearInterval(timer);
    timer = null;
    if (state.live) timer = setInterval(load, REFRESH_MS);
  }

  // The lines are geometry over live DOM, so anything that moves a card has to move them too. A
  // window resize is only one such thing — the sidebar sliding open resizes the content area without
  // any window event at all, which is why the wires used to detach from their cards until the next
  // render. A ResizeObserver on the content area catches every case, including each frame of that
  // animation, and one redraw per frame is affordable because a redraw is now just new path data.
  function watchLayout() {
    const root = document.getElementById('contentBody');
    let pending = false;
    const redraw = () => {
      if (pending) return;
      pending = true;
      requestAnimationFrame(() => { pending = false; drawLinks(); drawNgfwLinks(); });
    };

    if (root && window.ResizeObserver) new ResizeObserver(redraw).observe(root);
    else window.addEventListener('resize', debounce(() => { drawLinks(); drawNgfwLinks(); }, 120));

    // The regions list scrolls inside its own frame now, and scrolling moves cards without resizing
    // anything — no resize event, no observer callback, and the wires would stay pinned where the
    // cards used to be. Scroll does not bubble, so this listens in the capture phase and catches
    // every scroller under the page, whichever one the pointer is over.
    (root || document).addEventListener('scroll', redraw, true);
  }

  function mount() {
    load();
    schedule();
    watchLayout();
    if (window.NMS && window.NMS.onRefresh) window.NMS.onRefresh(load);
  }

  function debounce(fn, ms) {
    let t = null;
    return function () { clearTimeout(t); t = setTimeout(fn, ms); };
  }

  document.addEventListener('DOMContentLoaded', mount);
}());
