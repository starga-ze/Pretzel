// Static wiring check for mgmtd/www. Run after moving code between files:
//
//   node tests/check_frontend_wiring.cjs
//
// It cannot tell you the page looks right. What it can tell you — and what a refactor of files
// held together by load order and a shared `window.NMS` namespace actually gets wrong — is:
//
//   1. every file still parses;
//   2. every `NMS.foo` a file reads is assigned by some file;
//   3. every page loads the files that assign what its own scripts read;
//   4. nothing assigned on NMS has quietly disappeared (compare against the recorded baseline).
//
// (3) is the one that matters most: scripts are plain <script> tags, so a page that lists them in
// the wrong order, or omits one, fails at runtime and only on that page.
const fs = require('fs');
const path = require('path');

const WWW = path.join(__dirname, '..', 'mgmtd', 'www');
const JS = path.join(WWW, 'js');
const BASELINE = path.join(__dirname, 'frontend_api.json');

const walk = (dir, out = []) => {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (e.name === '__pycache__') continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (e.name.endsWith('.js')) out.push(p);
  }
  return out;
};

const rel = p => path.relative(JS, p).replace(/\\/g, '/');
const files = walk(JS).sort();
const src = new Map(files.map(p => [rel(p), fs.readFileSync(p, 'utf8')]));

let problems = 0;
const fail = (msg) => { problems++; console.log('  FAIL ' + msg); };

// ── 1. parse ────────────────────────────────────────────────────────────────
console.log('\nparse');
for (const [name, text] of src) {
  try { new (require('vm').Script)(text, { filename: name }); }
  catch (e) { fail(`${name}: ${e.message}`); }
}
if (!problems) console.log(`  ok   ${src.size} files parse`);

// ── 2 & 4. the NMS surface ──────────────────────────────────────────────────
// Assignments: `window.NMS.x =`, `NMS.x =`, and the object-literal form `window.NMS = { x: ... }`
// is not used here, so the two patterns cover it.
const ASSIGN = /\b(?:window\.)?NMS\.([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)/g;
const READ = /\b(?:window\.)?NMS\.([A-Za-z_][A-Za-z0-9_]*)/g;

// Layers core may reach UP into when they happen to be loaded, and must not require. Core runs on
// every page; the staging layer only where a configuration candidate can exist. Every reference to
// these is guarded at its call site, and this list is what makes adding a third one deliberate
// rather than something a page discovers at runtime.
const OPTIONAL = new Set(['draft', 'staging']);

const assignedBy = new Map();   // symbol -> Set(file)
const readBy = new Map();       // symbol -> Set(file)
for (const [name, text] of src) {
  for (const m of text.matchAll(ASSIGN)) {
    if (!assignedBy.has(m[1])) assignedBy.set(m[1], new Set());
    assignedBy.get(m[1]).add(name);
  }
  for (const m of text.matchAll(READ)) {
    if (!readBy.has(m[1])) readBy.set(m[1], new Set());
    readBy.get(m[1]).add(name);
  }
}

console.log('\nNMS symbols');
let dangling = 0;
for (const [sym, readers] of readBy) {
  if (assignedBy.has(sym)) continue;
  dangling++;
  fail(`NMS.${sym} is read by [${[...readers].join(', ')}] but never assigned`);
}
if (!dangling) console.log(`  ok   ${assignedBy.size} symbols, all assigned`);

// ── 3. per-page load order ──────────────────────────────────────────────────
// A page's scripts may only read a symbol that an EARLIER script on the same page assigns —
// except through a function called later, which is the normal case, so this only insists that
// some script on the page assigns it at all.
console.log('\npage wiring');
const pages = fs.readdirSync(WWW).filter(f => f.endsWith('.html'));
for (const page of pages) {
  const html = fs.readFileSync(path.join(WWW, page), 'utf8');
  const listed = [...html.matchAll(/src="js\/([^"]+)"/g)].map(m => m[1]);
  const missingFile = listed.filter(f => !src.has(f));
  if (missingFile.length) { fail(`${page} lists missing file(s): ${missingFile.join(', ')}`); continue; }
  if (!listed.length) continue;

  const provided = new Set();
  listed.forEach(f => (src.get(f).matchAll(ASSIGN), 0));
  listed.forEach(f => { for (const m of src.get(f).matchAll(ASSIGN)) provided.add(m[1]); });

  const needed = new Set();
  listed.forEach(f => { for (const m of src.get(f).matchAll(READ)) needed.add(m[1]); });

  const gaps = [...needed].filter(s => !provided.has(s) && !OPTIONAL.has(s));
  if (gaps.length) fail(`${page}: reads NMS.{${gaps.join(', ')}} but no script on the page assigns them`);
}
if (!problems) console.log(`  ok   ${pages.length} pages wired`);

// ── 4. baseline ─────────────────────────────────────────────────────────────
const current = Object.fromEntries([...assignedBy].map(([k, v]) => [k, [...v].sort()]).sort());
if (process.argv.includes('--write')) {
  fs.writeFileSync(BASELINE, JSON.stringify(current, null, 2) + '\n');
  console.log(`\nbaseline written: ${Object.keys(current).length} symbols`);
} else if (fs.existsSync(BASELINE)) {
  console.log('\nbaseline');
  const before = JSON.parse(fs.readFileSync(BASELINE, 'utf8'));
  const lost = Object.keys(before).filter(k => !(k in current));
  const added = Object.keys(current).filter(k => !(k in before));
  lost.forEach(k => fail(`NMS.${k} disappeared (was in ${before[k].join(', ')})`));
  if (added.length) console.log(`  note new symbols: ${added.join(', ')}`);
  if (!lost.length) console.log(`  ok   all ${Object.keys(before).length} recorded symbols still assigned`);
} else {
  console.log('\nbaseline: none recorded — run with --write to record the current surface');
}

console.log(problems ? `\n${problems} problem(s)\n` : '\nall checks passed\n');
process.exit(problems ? 1 : 0);
