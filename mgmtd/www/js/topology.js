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
 * first. RN and SC are therefore drawn as real lanes in the picture but marked "API pending" — the
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
                  note: 'L4 tunnel termination', egress: true },
    swg_proxy:  { label: 'MU-SPN · Explicit Proxy', sub: 'Secure Web Gateway', tone: 'swg', flow: 'swg',
                  note: 'L7 proxy — PAB and PAC sessions', egress: true },
    gp_portal:  { label: 'GP Portal', sub: 'agent config + gateway list', tone: 'portal', flow: 'ctl',
                  note: 'control plane', egress: false },
    // Remote networks are onboarded to a Prisma Access location like any other service, and this same
    // API reports them — service_ip is where the branch's IPsec tunnel lands, plus the egress IPs.
    remote_network: { label: 'RN-SPN · Remote Network', sub: 'branch IPsec termination', tone: 'rn',
                      flow: 'rn', note: 'service IP + egress for branch traffic', egress: true },
  };
  const svcOf = (k) => SVC[k] || { label: k || 'unknown service', sub: '', tone: 'other', flow: 'mu',
                                   note: 'unrecognised serviceType', egress: false };

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
  };

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
    egressOpen: false,  // the egress address list — the rest are one click away
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
        auxOnly: list.every(g => !g.nodes.length && !g.lbs.length && g.aux.length),
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

    // Data-plane regions first — that is where user traffic actually lands.
    zones.sort((a, b) => (b.dataplane - a.dataplane) || (a.auxOnly - b.auxOnly) || a.name.localeCompare(b.name));
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
    const scope = `${t.length} SASE tenant${t.length === 1 ? '' : 's'} · ${fw.length} NGFW`;

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
  function endCard(id, tone, icon, name, sub, body, pending) {
    return `<div class="topo-end ${pending ? 'is-pending' : ''}" id="${id}"
                 style="--topo-tone:var(--tc-${tone})">
        <div class="topo-end-h">
          <span class="topo-end-ic"><svg viewBox="0 0 24 24">${icon}</svg></span>
          <span><span class="topo-end-nm">${esc(name)}</span><div class="topo-end-sub">${esc(sub)}</div></span>
        </div>
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
          <span class="topo-lane-nm">Browser + PAC</span><span class="topo-lane-l">L7</span></div>
        <div class="topo-note">In full tunnel the browser session rides the L4 tunnel first and is
          proxied by the SWG behind it. Under split tunnel it reaches the SWG directly.</div>`);

    // Remote networks are NOT waiting on another API: this one reports serviceType remote_network for
    // every region a branch is onboarded to, and those show up as RN-SPN inside the regions. With
    // none onboarded there is no RN-SPN to draw and no traffic to imply, so the endpoint sits here
    // disabled and unconnected rather than as a lane that looks like it is waiting for something.
    const rn = c.rn
      ? endCard('end-rn', 'rn', ICONS.branch, 'Remote Users', 'RN · users behind a branch IPsec tunnel', `
          <div class="topo-end-line"><span class="topo-mini-dot" style="--topo-tone:var(--tc-rn)"></span>
            RN-SPN addresses <b>${c.rn}</b></div>
          <div class="topo-note">Each branch tunnel lands on its region's service IP. BGP routes and
            per-branch bandwidth still need the remote-network read.</div>`)
      : endCard('end-rn', 'pending', ICONS.branch, 'Remote Users', 'RN · users behind a branch IPsec tunnel', `
          <div class="topo-end-line"><span class="topo-tag pending">not configured</span></div>
          <div class="topo-note">No remote network is onboarded in this tenant, so no RN-SPN exists to
            draw.</div>`, true);

    return `<div class="topo-col">${mu}${rn}</div>`;
  }

  function destColumn(m) {
    // An allow-list is written per region as often as it is written whole, so each address says where
    // it egresses from. The list folds like the data-plane frame: a few, then the rest on request.
    const ips = m.counts.egressIps;
    const LIMIT = 4;
    const list = state.egressOpen ? ips : ips.slice(0, LIMIT);
    const rows = list.map(x => `<span class="topo-ip">${esc(x.address)}
        <em>(${esc(x.zone)})</em></span>`).join('');
    const more = ips.length > LIMIT
      ? `<button class="topo-more" id="egressMore" type="button">${state.egressOpen
          ? '− show fewer' : '+ ' + (ips.length - LIMIT) + ' more'}</button>`
      : '';
    const net = endCard('dst-net', 'ok', ICONS.globe, 'Internet & SaaS', 'egress from the fabric', `
        <div class="topo-end-line">Egress addresses <b>${ips.length}</b></div>
        <div class="topo-ips">${rows}</div>${more}
        <div class="topo-note">The source IPs a SaaS allow-list must carry.</div>`);

    const fw = ngfwForSite().map(d => `<button class="topo-fw" type="button" data-fw="${esc(d.oid)}"
          title="${esc((d.name || d.target) + ' · ' + (d.target || '') + (d.site_name ? ' · ' + d.site_name : ''))}">
        <span class="topo-fw-dot ${d.status === 'active' ? 'ok' : d.status === 'down' ? 'bad' : ''}"></span>
        <span class="topo-fw-nm">${esc(d.name || d.target || 'unnamed')}</span>
        <span class="topo-fw-t">${esc(d.target || '')}</span>
      </button>`).join('');

    // Both destinations are targets, so they sit at the top of the destination column with the
    // other target.
    return `<div class="topo-col">
        ${net}
        <div class="topo-priv-group">
          ${endCard('dst-fw', 'ok', ICONS.shield, 'On-premise NGFW', 'where private-app policy is enforced', `
            ${fw ? `<div class="topo-fw-list">${fw}</div>`
                 : '<div class="topo-note">No NGFW is managed in this scope.</div>'}
            <div class="topo-note">Which Service Connection or connector reaches which firewall is
              not readable yet, so no link is drawn between them.</div>`)}
          ${endCard('dst-apps', 'pending', ICONS.server, 'Private apps & Data Center', 'behind the firewall', `
            <div class="topo-end-line"><span class="topo-tag pending">not configured</span></div>
            <div class="topo-note">Applications, servers and segments the firewall fronts. What each
              Service Connection or connector publishes is not readable from this API.</div>`, true)}
        </div>
      </div>`;
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

  function svcGroup(z, g, gi) {
    const lbCards = g.lbs.map(n => nodeCard(n, 'is-lb' + (n.lbActive === false ? ' is-standby' : '') +
                                               (n.isNew ? ' is-new' : ''))).join('')
      || '<span class="topo-empty-slot">direct</span>';

    const nodeCards = g.nodes.map(n => nodeCard(n, n.isNew ? 'is-new' : '')).join('');
    const auxCards = g.aux.map(n => nodeCard(n, 'is-aux' + (n.isNew ? ' is-new' : ''))).join('');
    const goneCards = (g.goneNodes || []).map(x =>
      `<span class="topo-node is-gone"><span class="topo-node-ip">${esc(x.address)}</span>
         <span class="topo-node-sub">withdrawn</span></span>`).join('');

    const body = (nodeCards || auxCards || goneCards)
      ? nodeCards + auxCards + goneCards
      : '<span class="topo-empty-slot">no nodes</span>';

    // A group with nothing but auxiliary addresses is not a regional service — the tenant's global
    // auth cache arrives under swg_proxy, and calling it "proxy nodes" would be wrong.
    const auxOnly = !g.nodes.length && !g.lbs.length && g.aux.length;
    // With no NLB carrying traffic there is no active/standby pair to show: every listed address is
    // in service, and it is the portal's gateway list plus the app's own selection that decides
    // which one a given client lands on. Saying so is the only honest reading of this payload.
    const idleLb = g.svc === 'gp_gateway' && g.lbs.some(n => n.lbActive === false);
    const note = auxOnly ? 'shared global service, not a regional node'
               : idleLb ? 'no NLB in path — the app picks from the portal gateway list'
               : g.spec.note;

    return `<div class="topo-svc tone-${auxOnly ? 'other' : g.spec.tone}" id="svc-${z.zi}-${gi}">
        <div class="topo-svc-h">
          <div class="topo-svc-nm">${esc(auxOnly ? (ADDR_LABEL[g.aux[0].addressType] || g.spec.label) : g.spec.label)}</div>
          ${g.spec.sub && !auxOnly ? `<div class="topo-svc-sub">${esc(g.spec.sub)}</div>` : ''}
          <div class="topo-svc-note">${esc(note)}</div>
        </div>
        <div class="topo-slot"><span class="topo-slot-l">NLB</span>${lbCards}</div>
        <div class="topo-slot"><span class="topo-slot-l">Nodes · ${g.nodes.length + g.aux.length}</span>
          <div class="topo-nodes">${body}</div></div>
      </div>`;
  }

  function zoneLane(z) {
    const gw = z.groups.find(g => g.svc === 'gp_gateway');
    const swg = z.groups.find(g => g.svc === 'swg_proxy');
    const kind = z.dataplane ? (gw ? 'gateway region' : 'proxy region') : (z.auxOnly ? 'shared service' : 'portal region');

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
          <span class="topo-zone-kind">${esc(kind)}</span>${delta}
          <span class="topo-zone-counts">${counts}</span>
        </div>
        <!-- \`ep_\` in the API means Explicit Proxy, not endpoint: this is the tenant-wide proxy
             geo-LB name, which is why every region reports the same one. The per-region name is
             ep_regional_fqdn, on the proxy NLB itself. -->
        ${z.geoFqdn ? `<div class="topo-geo">proxy geo-LB <code>${esc(z.geoFqdn)}</code>
             <span class="topo-geo-note">tenant-wide</span></div>` : ''}
        <div class="topo-zone-body">${z.groups.map((g, gi) => svcGroup(z, g, gi)).join('')}</div>
      </div>`;
  }

  // The private estate, in one band across the foot of the canvas: the two ways in (Service
  // Connection and ZTNA Connector — a tenant may run either or both) on top, and what they reach
  // underneath. Keeping the four together is what makes every link between them a short hop.
  // The two ways into the customer's own estate — a tenant may run either or both. They belong at
  // the foot of the fabric column: the last thing inside Prisma Access before the picture crosses to
  // on-premise.
  // The private-application hand-off. The Service Connection half is still unread — no API reports
  // it yet — but the ZTNA half is now real: the connectors the tenant actually has, and whether each
  // one's tunnel and control plane are up.
  //
  // A connector is drawn as its own endpoint, not as something hanging off a firewall. They run on
  // hosts behind the customer's network and dial OUT to the fabric themselves; the firewall they sit
  // behind is not a peer and there is no link to draw between them.
  function privLanes(tenant) {
    const z = (tenant && tenant.ztna) || {};
    const groups = Array.isArray(z.groups) ? z.groups : [];
    const conns = Array.isArray(z.connectors) ? z.connectors : [];
    const names = z.group_names || {};

    const healthy = conns.filter(c => (c.flags || {}).tunnel_up && (c.flags || {}).control_plane_up).length;
    const have = groups.length || conns.length;

    const connRows = conns.slice(0, 8).map(c => {
      const f = c.flags || {};
      const up = f.tunnel_up && f.control_plane_up;
      const why = !f.tunnel_up ? 'tunnel down' : !f.control_plane_up ? 'control plane down' : 'up';
      return `<div class="topo-ztna-row" title="${esc((c.cgnx_location || '') + ' · ' + why)}">
          <span class="topo-dot ${up ? 'ok' : 'bad'}"></span>
          <span class="topo-ztna-nm">${esc(c.name || c.oid || 'connector')}</span>
          <span class="topo-ztna-sub">${esc(names[c.group] || '')}</span>
          <span class="topo-ztna-ip">${esc(c.cgnx_vion_ip || '')}</span>
        </div>`;
    }).join('');

    const ztnaBody = have
      ? `<div class="topo-ztna-list">${connRows}</div>
         ${conns.length > 8 ? `<div class="topo-note">+ ${conns.length - 8} more</div>` : ''}
         <span class="topo-tag ${healthy === conns.length ? '' : 'pending'}">${
           groups.length} group${groups.length === 1 ? '' : 's'} · ${healthy}/${conns.length} up</span>`
      : `<div class="topo-note">connector VMs dial out to Zero Trust Tunnel termination points in the
           region — no routing from your network, so overlapping app subnets are fine, and the app can
           be reached without crossing the firewall</div>
         <span class="topo-tag pending">not collected</span>`;

    return `<div class="topo-priv-split" id="priv-stack">
        <div class="topo-half is-pending" id="dst-sc">
          <div class="topo-half-nm">Service Connection</div>
          <div class="topo-half-sub">SC-CAN · data plane</div>
          <div class="topo-note">user traffic reaches the data centre over the fabric and lands on a
            Corporate Access Node — which does no inspection of its own; that already happened on the
            SPN the session came from</div>
          <span class="topo-tag pending">API pending</span>
        </div>
        <div class="topo-half ${have ? '' : 'is-pending'}" id="dst-ztna">
          <div class="topo-half-nm">ZTNA Connector</div>
          <div class="topo-half-sub">ZTT · data plane${
            z.collected_at ? ` · read ${esc(relStamp(z.collected_at))}` : ''}</div>
          ${ztnaBody}
        </div>
      </div>`;
  }

  // Collapsed does not mean empty: one region stays on screen so the shape of a region is still
  // legible, and the rest are counted below it.
  const shownZones = (m) => state.planeOpen ? m.dataZones : m.dataZones.slice(0, 1);

  // The regions are the tallest thing on the page, and an operator watching the private hand-off or
  // comparing regions at the summary level does not always want all of it. Collapsed, the frame keeps
  // its counts and its links — it simply stops listing every node.
  function planeFrame(m) {
    const gw = m.counts.gw, swg = m.counts.swg;
    const nlb = m.dataZones.reduce((a, z) => a + z.groups.reduce((b, g) => b + g.lbs.length, 0), 0);
    const open = state.planeOpen;
    const shown = shownZones(m);
    const hidden = m.dataZones.length - shown.length;

    return `<div class="topo-plane ${open ? '' : 'is-closed'}" id="data-plane">
        <button class="topo-plane-h" id="planeToggle" type="button" aria-expanded="${open}">
          <svg class="topo-chev" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
               stroke-linecap="round"><polyline points="9 6 15 12 9 18"/></svg>
          <span class="topo-plane-t">Data plane · traffic regions</span>
          <span class="topo-plane-sum">${m.dataZones.length} region${m.dataZones.length === 1 ? '' : 's'}
            · GW ${gw} · SWG ${swg} · NLB ${nlb}</span>
        </button>
        <div class="topo-plane-b">${shown.map(zoneLane).join('')}</div>
        ${hidden ? `<button class="topo-more" id="planeMore" type="button">+ ${hidden} more
          region${hidden === 1 ? '' : 's'} · ${esc(m.dataZones.slice(1).map(z => z.name).join(', '))}</button>` : ''}
      </div>`;
  }

  // Portals and the global auth cache carry no user traffic, so they are chips at the head of the
  // column rather than lanes competing with the regions that do.
  function ctlStrip(m) {
    if (!m.ctlZones.length) return '';
    const chips = m.ctlZones.map((z, i) => z.groups.map((g, gi) => {
      const n = g.nodes.length + g.aux.length;
      const aux = !g.nodes.length && g.aux.length;
      return `<button class="topo-ctl-chip" type="button" id="ctl-${i}-${gi}"
                data-ctl="${esc(z.name)}" title="${esc(z.name + ' · ' + g.svc)}">
          <span class="topo-mini-dot" style="--topo-tone:var(--tc-${aux ? 'other' : 'portal'})"></span>
          <span class="topo-ctl-nm">${esc(aux ? 'Auth cache' : 'GP Portal')}</span>
          <span class="topo-ctl-z">${esc(z.name)}</span>
          <b>${n}</b>
        </button>`;
    }).join('')).join('');

    return `<div class="topo-ctl" id="ctl-strip">
        <span class="topo-ctl-t">Control plane &amp; shared services</span>
        <div class="topo-ctl-row">${chips}</div>
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
            · <b>${regions}</b> traffic region${regions === 1 ? '' : 's'}
            · <b>${ips}</b> egress address${ips === 1 ? '' : 'es'}</span>
        </div>
        <div class="topo-cols">
          ${edgeColumn(m)}
          <div class="topo-col">
            ${ctlStrip(m)}
            ${planeFrame(m)}
            ${privLanes(m.tenant)}
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
        <button class="topo-fwb-more" type="button" data-fwdetail="${esc(d.oid)}">Open detail</button>
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
  // way the screen will travel. It lives on the canvas frame rather than inside a deck: a deck can be
  // taller than the frame and scroll, and a control that scrolls away is a control you cannot find.
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

  // Per deck: the two draw different relationships and a combined key would ask the operator to
  // ignore half of it. The colours themselves are shared, so a pink line means branch IPsec on
  // either screen — which is the point of keeping one palette across both.
  function legendHtml() {
    const lg = (color, text) => `<span class="topo-lg"><i style="background:${color}"></i>${esc(text)}</span>`;

    if (state.view === 'ngfw') {
      return `<div class="topo-legend">
          ${lg('#2dd4bf', 'Service Connection → Prisma Access')}
          ${lg('#f472b6', 'Remote Network → Prisma Access')}
          ${lg('#34d399', 'IPsec to an external peer')}
          ${lg('#38bdf8', 'GlobalProtect — remote users on-premise')}
          ${lg('#2dd4bf', 'Port badge VPN — an IKE gateway terminates here')}
          ${lg('#f472b6', 'Port role WAN — a public address')}
          ${lg('#fbbf24', 'Port role EDGE — private, but a VPN/GP endpoint terminates here')}
          ${lg('#34d399', 'Port role LAN — RFC1918, nothing external')}
        </div>`;
    }

    return `<div class="topo-legend">
        ${lg('#38bdf8', 'GP tunnel → MU-SPN')}
        ${lg('#f472b6', 'Branch IPsec → RN-SPN')}
        ${lg('#a78bfa', 'Explicit Proxy → MU-SPN')}
        ${lg('#c084fc', 'full tunnel → SWG (L4→L7)')}
        ${lg('#fbbf24', 'Portal (control plane)')}
        ${lg('#22d3ee', 'NLB → node')}
        ${lg('#34d399', 'Egress to Internet / SaaS')}
        ${lg('#6b7280', 'RN-SPN / SC-CAN — API pending')}
      </div>`;
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
      `<div class="topo-canvas ${state.flow ? '' : 'no-flow'}" id="topoCanvas">
        <div class="topo-viewport" id="topoViewport">
          <div class="topo-decks" id="topoDecks">
            <section class="topo-deck ${state.view === 'fabric' ? 'is-active' : ''}"
                     id="deck-fabric">${composing ? composingHtml() : canvasHtml(model)}</section>
            <section class="topo-deck ${state.view === 'ngfw' ? 'is-active' : ''}"
                     id="deck-ngfw">${scopeChosen ? (composing ? composingHtml() : ngfwDeck()) : ''}</section>
          </div>
        </div>
        ${scopeChosen && !composing ? deckBtn(state.view === 'fabric' ? 'ngfw' : 'fabric') : ''}
      </div>` +
      (scopeChosen && !composing ? legendHtml() : '') +
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

    // The key belongs to the deck on screen, so it changes with it. Swapped rather than re-rendered:
    // a full render would restage both decks to relabel one strip.
    const legend = document.querySelector('.topo-legend');
    if (legend) legend.outerHTML = legendHtml();

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
      const gwGroup = z.groups.findIndex(g => g.svc === 'gp_gateway');
      const swgGroup = z.groups.findIndex(g => g.svc === 'swg_proxy');

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
    if (m.dataZones.length) {
      // No coarse RN line when nothing is onboarded: a line into the fabric would imply an RN-SPN
      // that does not exist. When one does exist it is drawn per region, above.

      out.push({ from: 'data-plane', to: 'dst-sc', kind: 'pending', zone: 'all' });
      out.push({ from: 'data-plane', to: 'dst-ztna', kind: 'pending', zone: 'all' });
    }

    // The portal is what the GlobalProtect app talks to before it has a gateway at all.
    if (m.ctlZones.length) out.push({ from: 'ep-gp', to: 'ctl-strip', kind: 'ctl', zone: 'all' });

    // The private estate sits in one band at the foot of the canvas, directly under the fabric it
    // hangs off: every link here is a short hop between neighbours. Routing them around the outside
    // of the diagram — which is what a distant right-hand column forced — was the clutter.
    out.push({ from: 'dst-sc', to: 'dst-fw', kind: 'pending', zone: 'all', route: 0 });
    out.push({ from: 'dst-ztna', to: 'dst-fw', kind: 'pending', zone: 'all', route: 1 });
    // The connector is a VM deployed in the application estate itself, so it also reaches private
    // apps directly — that path never passes the perimeter firewall, which is the whole point of it
    // and the reason it stays its own line.
    out.push({ from: 'dst-ztna', to: 'dst-apps', kind: 'pending', zone: 'all', route: 2 });
    out.push({ from: 'dst-fw', to: 'dst-apps', kind: 'pending', zone: 'all', short: true });
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

    const row = (k, v, mono) => v === '' || v === undefined || v === null
      ? '' : `<dt>${esc(k)}</dt><dd class="${mono ? 'mono' : ''}">${esc(v)}</dd>`;

    document.getElementById('topoDrawerT').textContent = n.address;
    document.getElementById('topoDrawerB').innerHTML = `
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
        <div class="topo-sub-list">${z.subnets6.map(s => `<span class="topo-sub">${esc(s)}</span>`).join('')}</div>` : ''}`;

    document.getElementById('topoDrawer').classList.add('open');
    document.querySelectorAll('.topo-node.selected').forEach(el => el.classList.remove('selected'));
    const el = document.querySelector('.topo-node[data-node="' + cssEscape(key) + '"]');
    if (el) el.classList.add('selected');
  }

  // The private-side firewalls are ours, so the drawer says what we actually know about one — and,
  // just as importantly, what we do not: which Service Connection reaches it.
  function openFwDrawer(oid) {
    const d = state.ngfw.find(x => x.oid === oid);
    if (!d) return;
    state.selected = null;
    const row = (k, v, mono) => (v === '' || v == null) ? ''
      : `<dt>${esc(k)}</dt><dd class="${mono ? 'mono' : ''}">${esc(v)}</dd>`;
    const word = d.status === 'active' ? 'reachable' : d.status === 'down' ? 'unreachable' : 'not probed yet';

    document.getElementById('topoDrawerT').textContent = d.name || d.target || 'firewall';
    document.getElementById('topoDrawerB').innerHTML = `
      <dl class="topo-kv">
        ${row('Role', 'On-premise NGFW')}
        ${row('Site', d.site_name)}
        ${row('Address', d.target, true)}
        ${row('Status', word)}
      </dl>
      ${fwDetail(d)}
      <div class="topo-drawer-sec">Why it is in this picture</div>
      <p class="field-hint">No Service Connection or ZTNA link is drawn — neither is reported by an
        API yet, and connectors are not peers of this firewall.</p>`;
    document.getElementById('topoDrawer').classList.add('open');
    document.querySelectorAll('.topo-node.selected').forEach(el => el.classList.remove('selected'));
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

    document.getElementById('egressMore')?.addEventListener('click', () => {
      state.egressOpen = !state.egressOpen;
      render();
    });

    document.getElementById('planeMore')?.addEventListener('click', () => {
      state.planeOpen = true;
      try { localStorage.setItem('topo.plane', 'open'); } catch (_) { /* private mode */ }
      render();
    });

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

      const det = e.target.closest('[data-fwdetail]');
      if (det) { openFwDrawer(det.dataset.fwdetail); return; }

      // The fabric deck's compact row (.topo-fw) opens the same drawer.
      const f = e.target.closest('[data-fw]');
      if (f) openFwDrawer(f.dataset.fw);
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
