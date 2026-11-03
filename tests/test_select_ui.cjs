// npm install --prefix /tmp/pretzel-select-ui-test jsdom@26.1.0
// NODE_PATH=/tmp/pretzel-select-ui-test/node_modules node --test tests/test_select_ui.cjs
//
// enhanceSelect replaces every native <select> in the console with a themed dropdown, so a fault
// in it takes out Configuration, the log viewer's two filters and every connector form at once.
// It is also invisible to a unit test over the module's exports: the one that shipped was a
// ReferenceError thrown from inside open(), which exists only while a click is being handled.
// So this drives the real thing — enhance a select, click the trigger, pick an option.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {JSDOM} = require('jsdom');
const path = require('node:path');
const source = file => readFileSync(path.join(__dirname, '../mgmtd/www/js', file), 'utf8');

// Every page loads session.js before utils.js. Both are evaluated here, in that order, because
// the bug was a declaration sitting in the first one while the second one read it.
function dom() {
  const d = new JSDOM(
    '<div id="host"><select id="s">'
    + '<option value="">All Services</option>'
    + '<option value="engined">engined</option>'
    + '<option value="mgmtd">mgmtd</option>'
    + '</select></div>',
    {url: 'https://appliance.test/log-viewer', runScripts: 'outside-only'});
  const w = d.window;
  // jsdom implements neither of these; every browser the console runs in does.
  w.Element.prototype.scrollIntoView = function () {};
  w.fetch = async () => ({ok: true, status: 200, json: async () => ({})});
  const errors = [];
  w.addEventListener('error', e => errors.push(String(e.error || e.message)));
  w.eval(source('core/utils.js'));
  return {w, errors};
}

const open = w => {
  w.document.querySelector('.cs-trigger')
   .dispatchEvent(new w.MouseEvent('click', {bubbles: true}));
  return w.document.querySelector('.cs-panel');
};

test('clicking the trigger opens a panel holding every option', () => {
  const {w, errors} = dom();
  w.NMS.utils.enhanceSelects(w.document.getElementById('host'));

  const trigger = w.document.querySelector('.cs-trigger');
  assert.ok(trigger, 'enhanceSelects rendered no trigger');
  assert.equal(trigger.querySelector('.cs-label').textContent, 'All Services');
  assert.equal(w.document.querySelector('.cs > select').id, 's', 'native select was not adopted');

  const panel = open(w);
  assert.deepEqual(errors, [], 'open() threw');
  assert.ok(panel, 'no panel after the click');
  assert.equal(panel.querySelectorAll('.cs-opt').length, 3);
  assert.equal(trigger.getAttribute('aria-expanded'), 'true');
});

test('picking an option writes through to the native select and fires change', () => {
  const {w} = dom();
  w.NMS.utils.enhanceSelects(w.document.getElementById('host'));

  let changed = null;
  w.document.getElementById('s').addEventListener('change', e => { changed = e.target.value; });

  open(w).querySelectorAll('.cs-opt')[1].dispatchEvent(new w.MouseEvent('click', {bubbles: true}));

  assert.equal(changed, 'engined', 'change did not fire with the picked value');
  assert.equal(w.document.getElementById('s').value, 'engined');
  assert.equal(w.document.querySelector('.cs-panel'), null, 'panel stayed open after a pick');
  assert.equal(w.document.querySelector('.cs-label').textContent, 'engined');
});

test('a second trigger closes the first one panel', () => {
  const {w, errors} = dom();
  const host = w.document.getElementById('host');
  host.insertAdjacentHTML('beforeend',
    '<select id="s2"><option value="">All Levels</option><option value="warn">Warn</option></select>');
  w.NMS.utils.enhanceSelects(host);

  const [a, b] = Array.from(w.document.querySelectorAll('.cs-trigger'));
  a.dispatchEvent(new w.MouseEvent('click', {bubbles: true}));
  b.dispatchEvent(new w.MouseEvent('click', {bubbles: true}));

  assert.deepEqual(errors, [], 'opening the second select threw');
  assert.equal(w.document.querySelectorAll('.cs-panel').length, 1, 'both panels were open at once');
  assert.equal(a.getAttribute('aria-expanded'), 'false');
  assert.equal(b.getAttribute('aria-expanded'), 'true');
});

test('clicking the trigger again closes its panel', () => {
  const {w} = dom();
  w.NMS.utils.enhanceSelects(w.document.getElementById('host'));
  assert.ok(open(w), 'the first click did not open a panel');
  assert.equal(open(w), null, 'a second click did not close the panel');
  assert.equal(w.document.querySelector('.cs-trigger').getAttribute('aria-expanded'), 'false');
});

test('enhancing twice does not stack a second trigger', () => {
  const {w} = dom();
  const host = w.document.getElementById('host');
  w.NMS.utils.enhanceSelects(host);
  w.NMS.utils.enhanceSelects(host);
  assert.equal(w.document.querySelectorAll('.cs-trigger').length, 1);
});
