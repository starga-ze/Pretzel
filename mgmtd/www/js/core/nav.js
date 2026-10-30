/* nav.js — everything that decides where you are and what you can reach.
 *
 * The single source of truth for the console's shape: which pages exist (PAGES), how
 * Configuration is grouped into tabs (SETTINGS_GROUPS), and what the sidebar lists (SIDEBAR_NAV).
 * The sidebar, its flyouts, its collapse state and its tiers are all renderings of that.
 *
 *   NMS.nav.PAGES / SETTINGS_GROUPS / SETTINGS_SECTIONS / groupOfTab / parseHref
 *   NMS.nav.build() / initSidebar() / initFlyouts() / initTiers() / syncActive(tabId)
 */
(function () {
  'use strict';

  window.NMS = window.NMS || {};

  // ── Configuration groups ─────────────────────────────────────────────────
  // One definition drives both navigation levels: the sidebar flyout lists the groups, and the
  // topbar shows the tabs of whichever group is open. A group with a single tab renders no tab
  // row; its name in the flyout is the whole story.
  //
  // `section` is what the group configures, and it is what the flyout puts its headings on. Without
  // it the flyout was one flat list of seven, and "AI Provider" sat next to "API Profile" with
  // nothing to say that one decides what the appliance itself says to a vendor while the other
  // describes the estate it manages — two words a letter apart doing entirely different jobs.
  //
  //   Infrastructure  Site Management   where things are, and what is there (a Site is one customer)
  //                   API Profile       the reusable definitions a connector references
  //                   API Connector     binding a device + credential + endpoints on a schedule
  //   AI              AI Provider       which vendors serve a turn (the pretzel-ai deployment)
  //                   AI Route          how a turn reaches a model, and who inspects it on the way
  //   System          User Management   who may sign in
  //
  // System Operation is NOT here. Saving and loading a configuration file, and the appliance's own
  // upkeep, are things you DO — they take effect when pressed, they stage nothing, and there is no
  // Publish for them. Everything else in this flyout is a declaration you edit and then publish,
  // and putting an action among them made Publish look like it applied to both.
  //
  // The order is the sidebar's own, read top to bottom: the column puts Infrastructure first, under
  // SITE, and AI after it, under SHARED. A flyout that opens off that column and then re-sorts what
  // it contains makes the operator hold two orders in their head for one set of things. System is
  // last in both, being the one an operator visits least.
  //
  // This decides where a bare /settings lands, since settingsDefaultTab is the first tab of the
  // first group — Sites, which is both the most-visited page here and the one everything else in
  // the estate is declared against.
  const SETTINGS_GROUPS = [
    { id: 'site-management', label: 'Site Management', section: 'infra', tabs: [
        { id: 'sites',        label: 'Sites'        },
        { id: 'devices',      label: 'Devices'      } ] },
    { id: 'api-profile', label: 'API Profile', section: 'infra', tabs: [
        { id: 'api-key',      label: 'API Credential' },
        { id: 'api-endpoint', label: 'API Endpoint' } ] },
    { id: 'api-connector', label: 'API Connector', section: 'infra', tabs: [
        { id: 'api-connector', label: 'API Connector' } ] },
    { id: 'ai-provider', label: 'AI Provider', section: 'ai', tabs: [
        { id: 'ai-provider', label: 'AI Provider' } ] },
    { id: 'ai-route', label: 'AI Route', section: 'ai', tabs: [
        { id: 'ai-route', label: 'AI Route' } ] },
    // One group each rather than two tabs under a "System Management" heading. They are not two
    // views of one subject the way Sites and Devices are: one is who may sign in, the other is what
    // the appliance does with its own disks and clock, and an operator going to either was reading
    // a tab row to find out they had landed on the wrong one. The tab ids are unchanged — users.js
    // and operation.js still key off `user` and `operation` — so only the way in moved.
    { id: 'user-management', label: 'User Management', section: 'system', tabs: [
        { id: 'user',         label: 'User Management'  } ] },
    // No `section`, deliberately: this group is promoted to its own sidebar entry under
    // Administrator, and the missing field is what keeps it out of the Configuration flyout. It
    // stays in this list because ?tab=operation still has to resolve to a group and a heading.
    //
    // `staging: false` for the same reason it has no section: it is not part of the candidate.
    // Its cards act on the appliance immediately — Load swaps the running configuration, Update
    // re-crawls — so there is nothing here for a Publish to send and nothing for a Revert to put
    // back. It is the one settings group that stages nothing (see buildTopbar).
    { id: 'system-operation', label: 'System Operation', staging: false, tabs: [
        { id: 'operation',    label: 'System Operation' } ] },
  ];

  // The label each section carries in the Configuration flyout. The first two are the sidebar's own
  // section names, spelled exactly as the column spells them — "Infra Service" and "AI Service"
  // named the same two groups in two different words, and the reader had to work out that they
  // were the same two groups. System has no counterpart up there because it is the appliance
  // itself rather than anything it manages or talks to.
  const SETTINGS_SECTIONS = { infra: 'Infrastructure', ai: 'AI', system: 'System' };

  const SETTINGS_TABS = SETTINGS_GROUPS.reduce((acc, g) => acc.concat(g.tabs), []);
  // Where a bare /settings (no ?tab=) lands. Published because the tab modules resolve the same
  // fallback independently — each renders only when its own tab is active, so a module hard-coding
  // a different default would paint its page under another group's heading.
  window.NMS = window.NMS || {};
  window.NMS.settingsDefaultTab = SETTINGS_TABS[0].id;
  const groupOfTab = (tabId) =>
    SETTINGS_GROUPS.find(g => g.tabs.some(t => t.id === tabId)) || SETTINGS_GROUPS[0];

  // ── Sidebar definition ───────────────────────────────────────────────────
  // Single source of truth for all pages.

  const SIDEBAR_NAV = [

    {
      type: 'link', id: 'home', label: 'Home', href: 'home',
      icon: `<path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>
             <polyline points="9 22 9 12 15 12 15 22"/>`,
    },

    // Everything here is read through one site. No section labels inside it: the four rows are one
    // kind of thing — what you do to a site — and a label per pair would be naming the obvious.
    { type: 'tier', id: 'site', label: 'Site', scoped: true,
      icon: `<path d="M12 21s7-5.686 7-11a7 7 0 1 0-14 0c0 5.314 7 11 7 11z"/>
             <circle cx="12" cy="10" r="2.6"/>` },



    // The two reads of the estate: its shape, and what its APIs are returning.
    { type: 'section', label: 'Infrastructure' },
    {
      // The estate as a picture rather than a list: who connects, what they land on, where it
      // exits. Named for what it draws, not for where it sits — the section above already says
      // that, and "Overview" would have been the only page here that did not say what it shows.
      type: 'link', id: 'site-topology', label: 'Topology', href: 'topology', scoped: true,
      icon: `<circle cx="5" cy="12" r="2.4"/><circle cx="12" cy="5.5" r="2.4"/>
             <circle cx="12" cy="18.5" r="2.4"/><circle cx="19" cy="12" r="2.4"/>
             <path d="M7 11l3-4M7 13l3 4M14 7l3 3.4M14 17l3-3.4"/>`,
    },
    {
      // The other half of the same estate: not its shape, but what its APIs are actually returning
      // and whether the collection cycle is still turning.
      type: 'link', id: 'api-collection', label: 'API Collection', href: 'collection', scoped: true,
      icon: `<path d="M4 7c0-1.7 3.6-3 8-3s8 1.3 8 3-3.6 3-8 3-8-1.3-8-3z"/>
             <path d="M4 7v5c0 1.7 3.6 3 8 3s8-1.3 8-3V7"/>
             <path d="M4 12v5c0 1.7 3.6 3 8 3s8-1.3 8-3v-5"/>`,
    },

    // Acting on the estate rather than reading it — which is why these are not filed with the two
    // above, and why they are the pages that will need a confirmation step.
    { type: 'section', label: 'Control' },
    {
      // The one row here that acts on a person rather than on a box: an end user hit a rule set to
      // adminApproval and is sitting in front of a dialog waiting on a yes or a no. It belongs with
      // Remote Access and Power Control because it is the same kind of errand — you come here to DO
      // something, and every row ends in a decision that takes effect the moment it is pressed.
      //
      // Named for the object, not for the errand, and deliberately without the word Access. "Access
      // Requests" would have put a second Access directly above Remote Access — one meaning an
      // operator reaching INTO a device, the other a user asking permission to send something OUT
      // of one. Opposite directions, same word, adjacent rows. "Approvals" was the other candidate
      // and, in a product that has a Publish button, reads as config-change approvals.
      //
      // "User Requests" is also what the vendor's own console and API call these, so an operator
      // who knows that screen finds the same noun here rather than having to map ours onto theirs.
      type: 'link', id: 'user-requests', label: 'User Requests', href: 'user-requests',
      scoped: true, badgeId: 'navUserRequests',
      icon: `<circle cx="9.5" cy="8" r="3.4"/>
             <path d="M3.5 20c0-3.2 2.7-5.4 6-5.4 1 0 1.9.2 2.7.5"/>
             <polyline points="14.4 17.4 16.7 19.7 21 15.4"/>`,
    },
    {
      type: 'link', id: 'remote-access', label: 'Remote Access', href: '#', soon: true,
      icon: `<polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/>`,
    },
    {
      type: 'link', id: 'power', label: 'Power Control', href: '#', soon: true,
      icon: `<path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/>`,
    },

    { type: 'tier', id: 'shared', label: 'Shared',
      icon: `<polygon points="12 2.5 3 7 12 11.5 21 7 12 2.5"/>
             <polyline points="3 16.5 12 21 21 16.5"/>
             <polyline points="3 11.75 12 16.25 21 11.75"/>` },
    { type: 'section', label: 'AI' },
    {
      // The internal assistant itself, not a view of it: the one page here that an ordinary
      // employee would use rather than an operator. It sits above Infra Service because the
      // security story on the pages below it starts with the traffic this page generates.
      type: 'link', id: 'assistant', label: 'Assistant', href: 'chatbot',
      icon: `<path d="M12 3l1.9 5.1L19 10l-5.1 1.9L12 17l-1.9-5.1L5 10l5.1-1.9z"/>
             <path d="M18.5 15.5l.8 2.2 2.2.8-2.2.8-.8 2.2-.8-2.2-2.2-.8 2.2-.8z"/>`,
    },
    {
      // The prompt sets the Assistant's guardrail is measured against. It sits beside the Assistant
      // rather than under Infra Service because it is the same subject read from the other end: one
      // page is the traffic, the other is the set the traffic is scored on.
      type: 'link', id: 'benchtest', label: 'Benchtest', href: 'benchtest',
      icon: `<path d="M4 20h16"/><rect x="5" y="11" width="3.6" height="6" rx="1"/>
             <rect x="10.2" y="7" width="3.6" height="10" rx="1"/>
             <rect x="15.4" y="13" width="3.6" height="4" rx="1"/>`,
    },

    // Its own section rather than a third entry under Infra Service. What sits up there is the
    // customer's estate — what they run and what it is doing; this is the VENDOR's product
    // documentation, which the assistant answers out of. Two different subjects that happen to
    // both be collected, and filing them together would make "Infra Service" mean "everything
    // with a crawler behind it".
    //
    // Named for what it provides rather than for what it holds, so a second source — release
    // notes, a customer's own runbooks — lands here without the section needing a new name.
    { type: 'section', label: 'Knowledge' },
    {
      // The corpus as a reader sees it. Collecting it is an action on the appliance and stays
      // under Administrator ▸ System Operation; this is where an operator comes to check what the
      // assistant can actually answer out of, which is a different errand.
      type: 'link', id: 'pa-tech-docs', label: 'PA Tech Docs', href: 'tech-doc',
      icon: `<path d="M12 6.6C10.4 5.1 8.4 4.6 6 4.6a1 1 0 0 0-1 1v11a1 1 0 0 0 1 1
                      c2.4 0 4.4.5 6 2"/>
             <path d="M12 6.6c1.6-1.5 3.6-2 6-2a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1
                      c-2.4 0-4.4.5-6 2"/>
             <path d="M12 6.6v13"/>`,
    },

    { type: 'section', label: 'Administrator' },
    {
      // A flyout group, not a link: the groups live here so the topbar carries a single row of
      // tabs. Each subitem opens its group's first tab.
      type: 'group', id: 'configuration', label: 'Configuration',
      // `section` is what puts a group in this flyout, and its absence is what takes it out. A
      // group promoted to a sidebar entry of its own drops the field and disappears from here,
      // while staying in SETTINGS_GROUPS — the tab still has to resolve, and the topbar still
      // reads its heading from the group. Mapped without this filter, a promoted group kept
      // rendering here under a blank section header, so it appeared in both places at once.
      subitems: SETTINGS_GROUPS.filter(g => g.section).map(g => ({
        id: g.id,
        label: g.label,
        section: SETTINGS_SECTIONS[g.section] || '',
        href: `settings?tab=${g.tabs[0].id}`,
        tabs: g.tabs.map(t => t.id),
      })),
      icon: `<circle cx="12" cy="12" r="3"/>
             <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06
                      a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09
                      A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83
                      l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09
                      A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83
                      l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09
                      a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83
                      l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09
                      a1.65 1.65 0 0 0-1.51 1z"/>`,
    },
    {
      // Beside Configuration rather than inside it. Same page, same tab machinery — only the way
      // in changed, because this is where an operator goes to act on the appliance rather than to
      // describe it.
      type: 'link', id: 'system-operation', label: 'System Operation', href: 'settings?tab=operation',
      icon: `<path d="M12 3v4"/><path d="M12 17v4"/><path d="M5.6 5.6l2.9 2.9"/>
             <path d="M15.5 15.5l2.9 2.9"/><path d="M3 12h4"/><path d="M17 12h4"/>
             <circle cx="12" cy="12" r="3.2"/>`,
    },
    { type: 'section', label: 'Monitor' },
    {
      type: 'link', id: 'system-log', label: 'System Log', href: 'log-viewer',
      icon: `<path d="M4 5h16"/><path d="M4 12h10"/><path d="M4 19h7"/>
             <path d="M15 16l2.5 3 3.5-6"/>`,
    },

    { type: 'section', label: 'Laboratory' },
    {
      type: 'link', id: 'laboratory', label: 'Laboratory', href: 'laboratory',
      icon: `<path d="M9 3h6"/>
             <path d="M10 3v6.5L4.6 18.2A2 2 0 0 0 6.3 21h11.4a2 2 0 0 0 1.7-2.8L14 9.5V3"/>
             <line x1="7" y1="15" x2="17" y2="15"/>`,
    },
];

  function buildSvg(inner) {
    return `<svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">${inner}</svg>`;
  }

  // Per-page top layout: title + optional detail tabs. Single source of truth so every
  // page gets the same fixed header band (injected into #pageTopbar by buildTopbar).
  const PAGES = {
    'home':            { title: 'Home' },
    // The assistant stages nothing and has nothing to re-fetch on demand, so it carries neither
    // control. Its own header lives inside the page, where the session is.
    'chatbot':         { title: 'Assistant', bareTopbar: true },
    // Two tabs, plain navigation like the audit page: Test browses the set and launches a run,
    // Result reads runs that already happened. A run's result has to outlive the window it was
    // watched in — the modal is a view of a run in flight, not the only place its numbers exist.
    'benchtest':       { title: 'Benchtest', tabs: [
                           { id: 'test',   label: 'Test'   },
                           { id: 'result', label: 'Result' } ] },
    // The group is chosen in the sidebar flyout (SETTINGS_GROUPS); the topbar shows that
    // group's name and one row of its tabs.
    //
    // `staging: true` — the only page whose tabs edit a candidate, so the only page that carries
    // Publish and Revert. A group inside it may opt out with its own `staging: false`.
    'settings':        { title: 'Configuration', groups: SETTINGS_GROUPS, staging: true },
    'topology':        { title: 'Topology' },
    'collection':      { title: 'API Collection' },
    'user-requests':   { title: 'User Requests' },
    'log-viewer':      { title: 'System Log' },
    'laboratory':      { title: 'Laboratory' },
    // Two ways in, for two errands: the sidebar's Knowledge Service entry for reading the corpus,
    // and the Tech Documentation card under System Operation for collecting it. The title matches
    // the sidebar label, because that is what the operator clicked to get here.
    'tech-doc':        { title: 'PA Tech Docs' },
  };

  // The flyout is rebuilt from the toggle's data-subitems each time it opens, so marking the
  // active group there is enough to keep it correct after an in-place tab switch.
  // Every group, not just Configuration by name. There is one flyout onto the settings page today,
  // but a selector that named it was a selector that would go quietly wrong the next time there
  // were two — which there briefly was.
  //
  // Groups are not the only way onto the settings page, which is why this judges the plain links
  // too: System Operation sits beside Configuration as an ordinary link to `settings?tab=operation`.
  // A click on a link navigates, and a navigation rebuilds the whole sidebar — but a flyout subitem
  // switches the tab in place underneath it, and nothing then re-judged the link. So the highlight
  // it was built with survived next to the group's, and both read as the current page.
  function syncSidebarActive(tabId) {
    document.querySelectorAll('.nav-group .nav-group-toggle').forEach(toggle => {
      let subs;
      try { subs = JSON.parse(toggle.dataset.subitems || '[]'); } catch (_) { return; }
      let any = false;
      subs.forEach(s => {
        s.active = !!(s.tabs && s.tabs.indexOf(tabId) !== -1);
        any = any || s.active;
      });
      toggle.dataset.subitems = JSON.stringify(subs);
      // The toggle carries the highlight while the flyout is shut, which is almost always.
      toggle.classList.toggle('active', any);
    });

    // Same rule buildSidebar applies, on the tab being switched to rather than the one in the URL
    // bar: a link that pins no tab owns the whole page, one that pins a tab owns only that tab.
    document.querySelectorAll('a.nav-item[data-page]').forEach(a => {
      const h = parseHref(a.dataset.page);
      if (h.page !== 'settings') return;
      a.classList.toggle('active', !h.tab || h.tab === tabId);
    });
  }


  function parseHref(href) {
    const [page, query] = String(href || '').split('?');
    const p = new URLSearchParams(query || '');
    return { page, tab: p.get('tab') || '', proto: p.get('proto') || '' };
  }

  function buildSidebar() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;

    const activePage  = location.pathname.split('/').pop() || 'home';
    const params      = new URLSearchParams(location.search);
    const activeProto = params.get('proto') || '';
    // Settings without ?tab= lands on the first tab, so resolve it here too or the sidebar
    // would show no group highlighted on a bare /settings.
    const requestedTab = params.get('tab') || '';
    const activeTab = (activePage === 'settings' && !SETTINGS_TABS.some(t => t.id === requestedTab))
      ? SETTINGS_TABS[0].id
      : requestedTab;

    // A link/subitem is active when its page matches and any tab/proto it pins matches.
    const hrefActive = (href, disabled) => {
      if (disabled) return false;
      const h = parseHref(href);
      if (!h.page || h.page === '#' || h.page !== activePage) return false;
      if (h.tab   && h.tab   !== activeTab)   return false;
      if (h.proto && h.proto !== activeProto) return false;
      return true;
    };

    let html = `
      <div class="sidebar-brand">
        <div class="brand-icon">P</div>
        <span class="brand-name">Pretzel</span>
        <button class="sidebar-toggle" id="sidebarToggle" aria-label="Collapse sidebar">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
               stroke="currentColor" stroke-width="2" stroke-linecap="round">
            <rect x="3" y="3" width="18" height="18" rx="2"/>
            <line x1="9" y1="3" x2="9" y2="21"/>
            <polyline points="15 9 12 12 15 15"/>
          </svg>
        </button>
      </div>
      <nav class="sidebar-nav">`;

    // The nav has two tiers — what is read through one site, and what is not — and the tier is a
    // property of a RUN of items, not of any one of them. So a tier opens a wrapper and the next
    // tier closes it; the scoped one gets the rail down its edge that says how far the site reaches.
    let tierOpen = false;
    let bodyOpen = false;
    const closeTier = () => {
      let out = '';
      if (bodyOpen) { out += '</div></div>'; bodyOpen = false; }
      if (tierOpen) { out += '</div>'; tierOpen = false; }
      return out;
    };

    for (const item of SIDEBAR_NAV) {
      if (item.type === 'tier') {
        html += closeTier();
        // The label sits OUTSIDE the wrapper, so it keeps the same left edge as every other tier
        // label; the rail below it marks the thing it governs rather than the name of it.
        //
        // A scoped tier puts its switcher on that same line, at the far right — the heading and the
        // answer to it read as one row, and the list below stays a list of services rather than a
        // list with a control wedged into the top of it.
        // The glyph is the tier's identity: collapsed it is all that is left of the heading, so the
        // same one has to lead the word when there is room for the word.
        //
        // The heading is also the tier's switch. A caret leads it because that is what a row you
        // can fold looks like everywhere else in this console, and the whole line is the target —
        // a 10px word is not something to ask anyone to hit. The site switcher sits on this same
        // line and keeps its own clicks; the handler steps over anything inside a .nav-group.
        html += `<div class="nav-tier-label${item.scoped ? ' is-site' : ''}" data-tier="${item.id}"` +
                ` role="button" tabindex="0" aria-controls="navTierBody-${item.id}" aria-expanded="false">` +
                `<svg class="nav-tier-caret" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                      aria-hidden="true"><polyline points="9 6 15 12 9 18"/></svg>` +
                `<svg class="nav-tier-ic" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                      aria-hidden="true">${item.icon || ''}</svg>` +
                `<span class="nav-tier-name">${item.label}</span>` +
                (item.scoped
                  ? `<div class="nav-group nav-site" data-group-id="site-scope" id="navSite"></div>`
                  : '') +
                `</div>`;
        html += `<div class="nav-tier">`;
        tierOpen = true;
        // Its own box because a height can only be animated on something that has one. Both tiers
        // have one now: the scoped tier folds when it has no site to read through, and either
        // folds when the operator says so.
        html += `<div class="nav-tier-body" id="navTierBody-${item.id}"><div class="nav-tier-body-in">`;
        bodyOpen = true;
        continue;
      }

      if (item.type === 'section') {
        html += `<div class="nav-section-label">${item.label}</div>`;
        continue;
      }

      if (item.type === 'link') {
        const active = hrefActive(item.href, item.disabled);
        const cls = ['nav-item', active ? 'active' : '', item.disabled ? 'nav-disabled' : ''].filter(Boolean).join(' ');
        const href = item.disabled ? '#' : item.href;
        const tooltip = item.disabled ? `${item.label} (Coming soon)` : item.label;
        html += `<a class="${cls}" href="${href}" data-page="${item.href || ''}" data-tooltip="${tooltip}"${
          item.scoped ? ` data-scoped="1" data-label="${item.label}"` : ''}>
          ${buildSvg(item.icon)}
          <span class="nav-label">${item.label}</span>
          ${item.soon ? '<span class="nav-badge-soon">Coming soon</span>' : ''}
          ${item.badgeId ? `<span class="nav-badge" id="${item.badgeId}"></span>` : ''}
        </a>`;
        continue;
      }

      if (item.type === 'group') {
        // Flyout group: the toggle never navigates; hovering/clicking it reveals a
        // flyout (built by initFlyouts from data-subitems) to the right — works the
        // same expanded or collapsed. The group is "active" if a subitem is active.
        //
        // A subitem links to its group's first tab, so matching on the href alone would drop
        // the highlight the moment the operator moved to a second tab. When the subitem lists
        // the tabs it owns, membership decides instead.
        const subs = item.subitems.map(s => ({
          id: s.id, label: s.label, href: s.href, tabs: s.tabs || null,
          section: s.section || '',
          soon: !!s.soon,
          active: s.tabs
            ? (parseHref(s.href).page === activePage && s.tabs.indexOf(activeTab) !== -1)
            : hrefActive(s.href, false),
        }));
        const groupActive = subs.some(s => s.active);
        const cls = ['nav-item', 'nav-group-toggle', groupActive ? 'active' : ''].filter(Boolean).join(' ');
        const dataSubs = JSON.stringify(subs).replace(/&/g, '&amp;').replace(/"/g, '&quot;');
        html += `<div class="nav-group" data-group-id="${item.id}">
          <button type="button" class="${cls}" data-tooltip="${item.label}"
                  data-label="${item.label}" data-subitems="${dataSubs}" aria-haspopup="true" aria-expanded="false">
            ${buildSvg(item.icon)}
            <span class="nav-label">${item.label}</span>
            ${item.badgeId ? `<span class="nav-badge" id="${item.badgeId}"></span>` : ''}
            <svg class="nav-chevron" width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2" stroke-linecap="round">
              <polyline points="9 6 15 12 9 18"/>
            </svg>
          </button>
        </div>`;
      }
    }

    html += `</nav>
      <div class="sidebar-footer">
        <button type="button" class="nav-item nav-theme" id="navTheme" data-tooltip="Theme"
                aria-pressed="false">
          <span class="nav-theme-ic" id="navThemeIc"></span>
          <span class="nav-label" id="navThemeLabel">Dark mode</span>
          <span class="nav-sw" aria-hidden="true"><span class="nav-sw-knob"></span></span>
        </button>
        <div class="nav-item nav-user" id="navUser" data-tooltip="Signed in">
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/>
            <circle cx="12" cy="7" r="4"/>
          </svg>
          <span class="nav-label nav-user-name" id="navUserName">--</span>
        </div>
        <button type="button" class="nav-item nav-logout" id="navLogout" data-tooltip="Log out">
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/>
            <polyline points="16 17 21 12 16 7"/>
            <line x1="21" y1="12" x2="9" y2="12"/>
          </svg>
          <span class="nav-label">Log out</span>
        </button>
        <div class="nav-item nav-version" data-tooltip="v0.1.0-dev"
             style="cursor:default;opacity:.35;pointer-events:none;">
          <svg class="nav-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor">
            <circle cx="12" cy="12" r="10"/>
            <line x1="12" y1="8" x2="12" y2="12"/>
            <line x1="12" y1="16" x2="12.01" y2="16"/>
          </svg>
          <span class="nav-label">v0.1.0-dev</span>
        </div>
      </div>`;

    html += closeTier();

    sidebar.innerHTML = html;

    // The switcher is cached state rather than markup, so it is filled once the nav is in the DOM.
    window.NMS.utils.syncSidebarScope?.();
  }

  // ── Sidebar collapse/expand ───────────────────────────────────────────────

  const COLLAPSED_KEY = 'sidebarCollapsed';

  // Pages that carry a list of their own down the left and therefore want the icon rail: two
  // full-width lists side by side is the layout the icon rail exists to avoid. Named here rather
  // than declared by each page so the OUTGOING animation can know it too — the rail folds on the
  // way out, before the navigation, instead of the next document appearing with a different
  // geometry. (Those pages still stamp html.sb-init-collapsed pre-paint for a direct load or a
  // refresh, where there is no click to animate.)
  const RAIL_COLLAPSED_PAGES = new Set(['chatbot']);
  const pageOf = (href) => String(href || '').split('?')[0].split('/').pop();
  const wantsIconRail = (page) => RAIL_COLLAPSED_PAGES.has(page || '');

  function initSidebar() {
    const sidebar       = document.getElementById('sidebar');
    const overlay       = document.getElementById('sidebarOverlay');
    const railToggle    = document.getElementById('railToggle');
    const railToggleBtn = document.getElementById('railToggleBtn');
    if (!sidebar) return;

    const isMobile = () => window.innerWidth <= 680;

    function isCollapsed() { return sidebar.classList.contains('collapsed'); }

    function applyCollapsed(collapsed) {
      sidebar.classList.toggle('collapsed', collapsed);
      if (railToggle)
        railToggle.style.display = (!isMobile() && collapsed) ? 'flex' : 'none';
      // The rail ignores folds and the full-width nav honours them, so the width is one of the
      // inputs to how a tier stands — re-ask on every change, the pre-paint one included.
      window.NMS.syncTiers?.();
    }

    if (isMobile()) {
      sidebar.classList.remove('collapsed');
      if (railToggle) railToggle.style.display = 'flex';
    } else {
      // The page's own wish overrides the saved preference for the INITIAL state only: the toggle
      // still works here, and every other page opens the way the operator left it.
      applyCollapsed(wantsIconRail(location.pathname.split('/').pop())
                     || localStorage.getItem(COLLAPSED_KEY) === 'true');
    }

    // The pre-paint width is now owned by the .collapsed class (applied above); drop
    // the bootstrap html class so it can't override width once the user toggles.
    document.documentElement.classList.remove('sb-init-collapsed');

    // Toggle button is injected by buildSidebar — need to bind after inject
    const toggleBtn = document.getElementById('sidebarToggle');
    toggleBtn?.addEventListener('click', () => {
      const next = !isCollapsed();
      applyCollapsed(next);
      localStorage.setItem(COLLAPSED_KEY, String(next));
    });

    // Fold on the way out, then navigate. Without this the rail is wide in the document being
    // left and narrow in the one being loaded, and the change reads as the page jumping rather
    // than as the rail closing — the two frames belong to different documents, so nothing ties
    // them together except doing the visible part before the load starts.
    //
    // The wait is bounded by a timer as well as by transitionend: a browser that skips the
    // transition (reduced motion, a background tab) fires no event, and a navigation that never
    // happened is a far worse failure than one that was not animated.
    // On `document`, not on the sidebar: the flyout groups are mounted on <body>, so a future
    // entry that lives in one would otherwise navigate without the fold. The guard below is narrow
    // enough that this cannot collide with the flyout's own same-page tab switching — it only
    // fires for a link whose target page wants the icon rail, and no tab switch does.
    document.addEventListener('click', (e) => {
      const link = e.target.closest('a.nav-item[href], a.nav-flyout-item[href]');
      if (!link || isMobile() || e.defaultPrevented) return;
      if (e.metaKey || e.ctrlKey || e.shiftKey || e.button !== 0) return;

      const href = link.getAttribute('href');
      if (!href || href === '#') return;
      if (!wantsIconRail(pageOf(href)) || isCollapsed()) return;

      e.preventDefault();
      applyCollapsed(true);

      let gone = false;
      const go = () => { if (!gone) { gone = true; location.href = href; } };
      sidebar.addEventListener('transitionend', function once(ev) {
        if (ev.propertyName !== 'width') return;
        sidebar.removeEventListener('transitionend', once);
        go();
      });
      setTimeout(go, 400);
    });

    function openMobile()  { sidebar.classList.add('mobile-open'); overlay?.classList.add('visible'); if (railToggle) railToggle.style.display = 'none'; }
    function closeMobile() { sidebar.classList.remove('mobile-open'); overlay?.classList.remove('visible'); if (railToggle) railToggle.style.display = 'flex'; }

    railToggleBtn?.addEventListener('click', () => {
      isMobile() ? openMobile() : applyCollapsed(false) || localStorage.setItem(COLLAPSED_KEY, 'false');
    });
    overlay?.addEventListener('click', closeMobile);

    window.addEventListener('resize', () => {
      if (!isMobile()) {
        sidebar.classList.remove('mobile-open');
        overlay?.classList.remove('visible');
        applyCollapsed(localStorage.getItem(COLLAPSED_KEY) === 'true');
      } else {
        sidebar.classList.remove('collapsed');
        applyCollapsed(false);
        if (railToggle) railToggle.style.display = 'flex';
      }
    });
  }

  // ── Nav group flyouts ─────────────────────────────────────────────────────
  // Groups don't expand inline; hovering/clicking the toggle reveals a flyout to the
  // right. The panel is a single shared element appended to <body> so the sidebar's
  // overflow never clips it — works identically whether the rail is expanded or
  // collapsed, satisfying "still selectable when collapsed".

  function initFlyouts() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;

    let flyout = document.getElementById('navFlyout');
    if (!flyout) {
      flyout = document.createElement('div');
      flyout.id = 'navFlyout';
      flyout.className = 'nav-flyout';
      document.body.appendChild(flyout);
    }

    let hideTimer = null;
    const cancelHide   = () => clearTimeout(hideTimer);
    const scheduleHide = () => { clearTimeout(hideTimer); hideTimer = setTimeout(hide, 160); };

    function hide() {
      flyout.classList.remove('visible');
      sidebar.querySelectorAll('.nav-group-toggle[aria-expanded="true"]')
        .forEach(t => t.setAttribute('aria-expanded', 'false'));
    }

    function show(group) {
      const toggle = group.querySelector('.nav-group-toggle');
      if (!toggle) return;

      let subs = [];
      try { subs = JSON.parse(toggle.dataset.subitems || '[]'); } catch (_) { /* ignore */ }

      // A heading is emitted whenever the section changes, so a flyout whose items carry none
      // renders exactly as it did before. Configuration is the only one that uses them today, and
      // it needs them: "AI Provider" and "API Profile" are two words a letter apart doing
      // different jobs, and a flat list of six gave the reader nothing to tell them apart by.
      //
      // The panel's own name is dropped when it has them. It would sit directly above the first
      // section heading in the same size, weight and colour — two stacked labels where the reader
      // expects one, the upper one naming the row they just pressed and so telling them nothing
      // they did not know a moment ago. A flyout with no sections keeps it: there it is the only
      // thing saying what the list is.
      const sectioned = subs.some(s => s.section);
      let section = null;
      flyout.innerHTML =
        (sectioned ? '' : `<div class="nav-flyout-title">${toggle.dataset.label || ''}</div>`) +
        subs.map(s => {
          let head = '';
          if ((s.section || '') !== (section || '')) {
            section = s.section || '';
            if (section) head = `<div class="nav-flyout-sec">${section}</div>`;
          }
          const cls  = ['nav-flyout-item', s.active ? 'active' : '', s.soon ? 'is-soon' : ''].filter(Boolean).join(' ');
          const href = s.soon ? '#' : s.href;
          const tab  = (s.tabs && s.tabs.length) ? ` data-goto-tab="${s.tabs[0]}"` : '';
          // A site row changes the scope rather than navigating, the same way a tab row switches in
          // place rather than reloading.
          const site = s.site ? ` data-set-site="${s.site}"` : '';
          const title = s.title ? ` title="${s.title}"` : '';
          return head + `<a class="${cls}" href="${href}"${tab}${site}${title}>` +
                 `<span>${s.label}</span>` +
                 (s.note ? `<span class="nav-flyout-note">${s.note}</span>` : '') +
                 (s.soon ? '<span class="nav-badge-soon">Soon</span>' : '') +
                 `</a>`;
        }).join('');

      // Already on the target page: switch in place rather than reloading, so the topbar and
      // the staged Publish state survive (see NMS.gotoSettingsTab).
      flyout.querySelectorAll('[data-set-site]').forEach(a => {
        a.addEventListener('click', (e) => {
          e.preventDefault();
          window.NMS.utils.siteScope.set(a.dataset.setSite);
          hide();
        });
      });

      flyout.querySelectorAll('[data-goto-tab]').forEach(a => {
        a.addEventListener('click', (e) => {
          if (typeof window.NMS.gotoSettingsTab !== 'function') return;
          e.preventDefault();
          window.NMS.gotoSettingsTab(a.dataset.gotoTab);
          hide();
        });
      });

      // Anchor to the right edge of the toggle, clamped to the viewport.
      const r = toggle.getBoundingClientRect();
      flyout.style.visibility = 'hidden';
      flyout.classList.add('visible');
      const fh = flyout.offsetHeight;
      let top = r.top;
      if (top + fh > window.innerHeight - 8) top = Math.max(8, window.innerHeight - 8 - fh);
      flyout.style.top  = `${top}px`;
      flyout.style.left = `${r.right + 6}px`;
      flyout.style.visibility = '';

      sidebar.querySelectorAll('.nav-group-toggle[aria-expanded="true"]')
        .forEach(t => t.setAttribute('aria-expanded', 'false'));
      toggle.setAttribute('aria-expanded', 'true');
    }

    // Delegated rather than bound per element: the site group's toggle is rebuilt whenever the
    // estate changes, and a listener attached to the button it replaced would be attached to
    // nothing. The group divs themselves are stable, so hover can stay on them.
    sidebar.querySelectorAll('.nav-group').forEach(group => {
      group.addEventListener('mouseenter', () => { cancelHide(); show(group); });
      group.addEventListener('mouseleave', scheduleHide);
    });
    sidebar.addEventListener('click', (e) => {
      const toggle = e.target.closest('.nav-group-toggle');
      if (!toggle) return;
      e.preventDefault();
      cancelHide();
      show(toggle.closest('.nav-group'));
    });

    flyout.addEventListener('mouseenter', cancelHide);
    flyout.addEventListener('mouseleave', scheduleHide);

    // Dismiss when clicking away, scrolling, or resizing (anchor would go stale).
    document.addEventListener('click', (e) => {
      if (!flyout.contains(e.target) && !e.target.closest('.nav-group')) hide();
    });
    window.addEventListener('scroll', hide, true);
    window.addEventListener('resize', hide);
  }


  // ── Nav tiers: fold and unfold ────────────────────────────────────────────
  // Per browser, not per user: which halves of the nav someone keeps open is a view preference
  // about this screen, the same kind of thing as the sidebar's own collapsed state, and it has no
  // business making a round trip.

  const TIERS_KEY = 'pz.nav.tiers';

  function tierPrefs() {
    try { return JSON.parse(localStorage.getItem(TIERS_KEY) || '{}') || {}; }
    catch (_) { return {}; }   // private window, or someone else's JSON under our key
  }

  // Open unless it has been shut. A tier absent from the store is a tier nobody has touched, and
  // the useful default for a nav is that you can see what is in it.
  function tierWanted(id) {
    return tierPrefs()[id] !== false;
  }

  function setTierWanted(id, open) {
    const prefs = tierPrefs();
    prefs[id] = open;
    try { localStorage.setItem(TIERS_KEY, JSON.stringify(prefs)); } catch (_) { /* nothing to do */ }
  }

  // The scoped tier answers to two things at once: the operator's fold, and whether there is a site
  // to read the rows through at all. With no site the rows would open onto pages that can only
  // apologise, so the gate wins and the caret shows shut — pressing the heading then does nothing
  // but record a preference for when a site is chosen.
  //
  // Folding is an expanded-mode control, and at 48px it is ignored — the preference is kept, not
  // applied. Two reasons, and the second is the one that decided it:
  //
  //   · There is nothing at that width to show the state with. The caret and the rail are both
  //     hidden, so a folded tier and a tier with nothing in it look identical.
  //   · Only one of the two tiers could be unfolded again. The SHARED heading collapses to a
  //     glyph, and pressing it folds; the SITE heading collapses to the site switcher, and
  //     pressing THAT opens the site list — the more valuable of the two things a 48px rail can
  //     offer. So SITE could be folded while expanded and then never reopened: a state the
  //     operator can reach and cannot leave.
  //
  // The gate still applies at either width: no site, no rows.
  function tierOpenState(id) {
    if (id === 'site' && !window.NMS.utils.siteScope.get()) return false;
    if (document.getElementById('sidebar')?.classList.contains('collapsed')) return true;
    return tierWanted(id);
  }

  function syncTiers() {
    document.querySelectorAll('.nav-tier-label[data-tier]').forEach((label) => {
      const id   = label.dataset.tier;
      const open = tierOpenState(id);
      label.classList.toggle('is-open', open);
      label.setAttribute('aria-expanded', String(open));
      document.getElementById(`navTierBody-${id}`)?.classList.toggle('is-open', open);
    });
  }
  window.NMS = window.NMS || {};
  window.NMS.syncTiers = syncTiers;   // the scope gate re-asks when the site changes

  function initTiers() {
    const sidebar = document.getElementById('sidebar');
    if (!sidebar) return;

    const toggle = (label) => {
      const id = label.dataset.tier;
      if (!id || sidebar.classList.contains('collapsed')) return;
      setTierWanted(id, !tierOpenState(id));
      syncTiers();
    };

    // Delegated: the heading is rewritten whenever the nav is rebuilt, and the site switcher inside
    // it is rewritten more often than that.
    sidebar.addEventListener('click', (e) => {
      if (e.target.closest('.nav-group')) return;   // the switcher's own business
      const label = e.target.closest('.nav-tier-label[data-tier]');
      if (label) toggle(label);
    });
    sidebar.addEventListener('keydown', (e) => {
      if (e.key !== 'Enter' && e.key !== ' ') return;
      if (e.target.closest('.nav-group')) return;
      const label = e.target.closest('.nav-tier-label[data-tier]');
      if (!label) return;
      e.preventDefault();                            // Space scrolls the nav otherwise
      toggle(label);
    });

    syncTiers();
  }

  window.NMS.nav = {
    PAGES, SETTINGS_GROUPS, SETTINGS_SECTIONS, SETTINGS_TABS,
    groupOfTab, parseHref, buildSvg,
    build: buildSidebar,
    initSidebar, initFlyouts, initTiers,
    syncActive: syncSidebarActive,
  };
})();
