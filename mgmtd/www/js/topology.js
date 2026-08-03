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

  const REFRESH_MS = 30000;      // the tenant probe itself runs on a 60s cycle
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
    site: '',           // '' = every site; otherwise a site oid
    tenants: [],
    ngfw: [],           // on-premise firewalls — the far end of a Service Connection
    region: 'all',      // kept for the per-region dimming; no longer exposed as a control
    live: true,
    flow: true,
    planeOpen: true,    // the data-plane frame — collapsed, the regions fold into one summary row
    egressOpen: false,  // the egress address list — the rest are one click away
    view: 'fabric',     // which deck is on screen: 'fabric' | 'ngfw'
    layers: {},         // device oid → { reachable, credential, api } from /api/status/devices
    selected: null,     // node key currently open in the drawer
    generatedAt: '',
    error: '',
  };

  try { state.planeOpen = localStorage.getItem('topo.plane') !== 'closed'; } catch (_) { /* private mode */ }

  // Everything on the page is scoped by site: the SASE tenant that serves it, and the firewalls that
  // sit in it. A site with no SASE tenant is a legitimate answer — the fabric deck says so.
  function siteList() {
    const seenSite = new Map();
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
    const opts = [`<option value="" ${state.site ? '' : 'selected'}>All sites</option>`].concat(
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
        <button class="topo-toggle ${state.live ? 'on' : ''}" id="topoLive" type="button">
          <span class="topo-live-dot"></span>Live</button>
        <span class="topo-stamp">${freshness(t[0])}</span>
      </div>`;
  }

  // Two different ages hide behind one word here, and conflating them would flatter the page: the
  // browser polls mgmtd every 30s, but the drawing is only as current as the tenant's last answer —
  // collectord probes on a 60s cycle and reports the previous cycle's outcome, so the fabric can be
  // a couple of minutes old while the poll is seconds old. sase_device.last_seen is the honest one.
  // (updated_at is not: config projection bumps it whether or not the tenant answered.)
  function freshness(tenant) {
    if (state.error) return `<b style="color:var(--red)">refresh failed</b> — last known`;
    const seen = tenant && tenant.last_seen;
    if (!seen) return `polled <b>${esc(state.generatedAt ? relStamp(state.generatedAt) : '—')}</b>`;

    const age = ageSeconds(seen);
    const stale = age > 180;   // three probe cycles: something is not answering
    return `tenant answered <b${stale ? ' style="color:var(--orange)"' : ''}>${esc(relStamp(seen))}</b>`;
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
    return new Date(t).toLocaleString();
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
  function privLanes() {
    return `<div class="topo-priv-split" id="priv-stack">
        <div class="topo-half is-pending" id="dst-sc">
          <div class="topo-half-nm">Service Connection</div>
          <div class="topo-half-sub">SC-CAN · data plane</div>
          <div class="topo-note">user traffic reaches the data centre over the fabric and lands on a
            Corporate Access Node — which does no inspection of its own; that already happened on the
            SPN the session came from</div>
          <span class="topo-tag pending">not configured</span>
        </div>
        <div class="topo-half is-pending" id="dst-ztna">
          <div class="topo-half-nm">ZTNA Connector</div>
          <div class="topo-half-sub">ZTT · data plane</div>
          <div class="topo-note">connector VMs dial out to Zero Trust Tunnel termination points in the
            region — no routing from your network, so overlapping app subnets are fine, and the app can
            be reached without crossing the firewall</div>
          <span class="topo-tag pending">not configured</span>
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
            ${privLanes()}
          </div>
          ${destColumn(m)}
        </div>
      </div>`;
  }

  // ── Deck 2: on-premise infrastructure ───────────────────────────────────────
  // The fabric deck ends at the firewall; this one starts there. It is built for tens of devices, so
  // it is a grid grouped by site rather than a diagram — a picture of forty firewalls wired to each
  // other would be a picture of nothing.
  const LAYER_WORD = { ok: 'active', fail: 'inactive', unknown: 'not configured' };

  function layerDots(oid) {
    const l = state.layers[oid] || {};
    const names = [['reachable', 'Device — ICMP reachability'], ['credential', 'Credential — API key'],
                   ['api', 'API — collection call']];
    return names.map(([k, title]) => {
      const v = l[k] || 'unknown';
      return `<span class="topo-lyr ${v}" title="${esc(title + ': ' + (LAYER_WORD[v] || v))}"></span>`;
    }).join('');
  }

  let fwIndex = 0;   // stagger counter, reset each render

  function fwCard(d) {
    const l = state.layers[d.oid] || {};
    const st = (l.reachable === 'ok' && l.credential === 'ok') ? 'ok'
             : (l.reachable === 'fail' || l.credential === 'fail') ? 'bad' : '';
    return `<button class="topo-fwc ${st}" type="button" data-fw="${esc(d.oid)}" style="--i:${fwIndex++}">
        <span class="topo-fwc-h">
          <span class="topo-fw-dot ${d.status === 'active' ? 'ok' : d.status === 'down' ? 'bad' : ''}"></span>
          <span class="topo-fwc-nm">${esc(d.name || d.target || 'unnamed')}</span>
        </span>
        <span class="topo-fwc-t">${esc(d.target || '—')}</span>
        <span class="topo-fwc-l">${layerDots(d.oid)}</span>
      </button>`;
  }

  function ngfwDeck() {
    fwIndex = 0;
    const all = ngfwForSite();
    if (!all.length) {
      return `<div class="topo-stage topo-stage-ngfw"><div class="topo-msg">
          <div><b>No firewall is managed here yet</b>
          Add one in <a href="settings?tab=devices">Configuration › Devices</a> — it will appear here
          and as the enforcement point on the fabric deck.</div>
        </div></div>`;
    }

    const sites = [];
    all.forEach((d) => {
      const key = d.site || '';
      let g = sites.find(x => x.key === key);
      if (!g) sites.push(g = { key, name: d.site_name || 'Unassigned', devs: [] });
      g.devs.push(d);
    });
    sites.sort((a, b) => (a.key ? 0 : 1) - (b.key ? 0 : 1) || a.name.localeCompare(b.name));

    let ok = 0, bad = 0, unk = 0;
    all.forEach((d) => { if (d.status === 'active') ok++; else if (d.status === 'down') bad++; else unk++; });

    return `<div class="topo-stage topo-stage-ngfw">
        <div class="topo-deck-h">
          <span class="topo-deck-t">NGFW Infrastructure</span>
          <span class="topo-deck-sum">
            <b>${all.length}</b> firewall${all.length === 1 ? '' : 's'}
            · <span class="ok">${ok} reachable</span>
            ${bad ? `· <span class="bad">${bad} unreachable</span>` : ''}
            ${unk ? `· <span class="muted">${unk} not probed</span>` : ''}
          </span>
        </div>
        <div class="topo-sites">
          ${sites.map(g => `<section class="topo-site">
              <div class="topo-site-h">${esc(g.name)}
                <span class="topo-site-n">${g.devs.length}</span></div>
              <div class="topo-fw-grid">${g.devs.map(fwCard).join('')}</div>
            </section>`).join('')}
        </div>
        <div class="topo-deck-note">Layer dots, left to right: device reachability · credential ·
          API collection. The private apps each firewall fronts, and which Service Connection or
          connector reaches it, are not readable yet.</div>
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

  function legendHtml() {
    const lg = (color, text) => `<span class="topo-lg"><i style="background:${color}"></i>${esc(text)}</span>`;
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

  function render() {
    const root = document.getElementById('contentBody');
    if (!root) return;
    const scoped = tenantsForSite();
    model = scoped.length ? buildModel(scoped[0]) : null;

    root.className = 'content-body topo-page';
    root.innerHTML = barHtml() +
      `<div class="topo-canvas ${state.flow ? '' : 'no-flow'}" id="topoCanvas">
        <div class="topo-viewport" id="topoViewport">
          <div class="topo-decks" id="topoDecks">
            <section class="topo-deck ${state.view === 'fabric' ? 'is-active' : ''}"
                     id="deck-fabric">${canvasHtml(model)}</section>
            <section class="topo-deck ${state.view === 'ngfw' ? 'is-active' : ''}"
                     id="deck-ngfw">${ngfwDeck()}</section>
          </div>
        </div>
        ${deckBtn(state.view === 'fabric' ? 'ngfw' : 'fabric')}
      </div>` +
      legendHtml() +
      `<aside class="topo-drawer" id="topoDrawer"><div class="topo-drawer-h">
          <span class="topo-drawer-t" id="topoDrawerT">Node</span>
          <button class="topo-drawer-x" id="topoDrawerX" type="button">&times;</button>
        </div><div class="topo-drawer-b" id="topoDrawerB"></div></aside>`;

    wire();
    requestAnimationFrame(() => { syncDeck(false); drawLinks(); });
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

    // The cards assemble on arrival, not on every poll — a stagger that replayed every 30 seconds
    // would read as the page glitching rather than as the estate resolving into view.
    const deck = document.getElementById('deck-' + view);
    if (deck) {
      deck.classList.add('is-entering');
      setTimeout(() => deck.classList.remove('is-entering'), 900);
    }

    // Links are measured from laid-out cards, and an inactive deck is scaled — so they are only
    // worth redrawing once the fabric deck is the one on screen and settled.
    if (view === 'fabric') setTimeout(drawLinks, 560);
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
    const stage = document.getElementById('topoStage');
    const svg = document.getElementById('topoLinks');
    if (!stage || !svg || !model) return;
    if (state.view !== 'fabric') return;   // the deck is scaled away; its rects would lie

    const origin = stage.getBoundingClientRect();
    const rectOf = (el) => {
      const r = el.getBoundingClientRect();
      return { l: r.left - origin.left, r: r.right - origin.left, t: r.top - origin.top,
               b: r.bottom - origin.top, cx: r.left + r.width / 2 - origin.left,
               cy: r.top + r.height / 2 - origin.top };
    };

    const specs = edgeSpecs(model);

    // One pass to work out where every link runs...
    const drawn = [];
    specs.forEach((sp) => {
      const a = elFor(sp.from), b = elFor(sp.to);
      if (!a || !b) return;
      drawn.push({ spec: sp, d: geometry(sp, rectOf(a), rectOf(b)),
                   dim: !(state.region === 'all' || sp.zone === 'all' || sp.zone === state.region) });
    });

    // ...and a second only if the set of links actually changed. A sidebar sliding open fires a
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
      path.setAttribute('id', 'fp-' + i);
      path.setAttribute('d', x.d);
      path.setAttribute('class', 'topo-link kind-' + x.spec.kind + (x.dim ? ' is-dimmed' : ''));
      svg.appendChild(path);
      paths.push({ path, spec: x.spec, dim: x.dim, id: 'fp-' + i });
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
        ${row('Created', n.created ? new Date(n.created * 1000).toLocaleString() + ' (' + relAge(n.age) + ' ago)' : '')}
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
      <div class="topo-drawer-sec">Why it is in this picture</div>
      <p class="field-hint">A Service Connection terminates on an SC-CAN, which performs no
        inspection — private-app policy is enforced here. Which Service Connection reaches this
        firewall is not readable yet, so no link is drawn to it.</p>`;
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
      siteSel.addEventListener('change', (e) => { state.site = e.target.value; render(); });
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
      const n = e.target.closest('.topo-node[data-node]');
      if (n) { openDrawer(n.dataset.node); return; }
      const f = e.target.closest('.topo-fw[data-fw]');
      if (f) openFwDrawer(f.dataset.fw);
    });

    const x = document.getElementById('topoDrawerX');
    if (x) x.addEventListener('click', closeDrawer);
  }

  // ── Data ────────────────────────────────────────────────────────────────────
  async function load() {
    try {
      const r = await fetch('/api/topology', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
      if (r.status === 401) { location.href = '/'; return; }
      const d = await r.json();
      state.tenants = Array.isArray(d.tenants) ? d.tenants : [];
      state.ngfw = Array.isArray(d.ngfw) ? d.ngfw : [];

      // The on-premise deck judges a firewall the way the Home page does — three dependency layers,
      // not just "did it answer a ping".
      try {
        const lr = await fetch('/api/status/devices', { credentials: 'same-origin', headers: { Accept: 'application/json' } });
        if (lr.ok) state.layers = (await lr.json()) || {};
      } catch (_) { /* keep the last snapshot */ }
      state.generatedAt = d.generated_at || '';
      state.error = '';
    } catch (e) {
      state.error = e.message || 'load failed';
    }
    render();
    if (state.selected) openDrawer(state.selected);
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
      requestAnimationFrame(() => { pending = false; drawLinks(); });
    };

    if (root && window.ResizeObserver) new ResizeObserver(redraw).observe(root);
    else window.addEventListener('resize', debounce(drawLinks, 120));
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
