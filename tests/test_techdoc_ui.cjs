// npm install --prefix /tmp/pretzel-corpus-ui-test jsdom@26.1.0
// NODE_PATH=/tmp/pretzel-corpus-ui-test/node_modules node --test tests/test_techdoc_ui.cjs
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {JSDOM} = require('jsdom');
const path = require('node:path');
const source = file => readFileSync(path.join(__dirname,'../mgmtd/www/js',file),'utf8');
const wait = ms => new Promise(r => setTimeout(r,ms));
async function until(predicate) {
  for (let i=0;i<100;i++) { if (predicate()) return; await wait(5); }
  throw Error('UI condition timed out');
}
const status = {documents:2,bodies:2,last_run_status:'ok',products:[
  {product:'ngfw',docset:'help',version:'12.1',documents:1},
  {product:'ngfw',docset:'admin',version:'11.1',documents:1}
]};
const doc = title => ({url:'https://docs.paloaltonetworks.com/ngfw/help/'+title,title,char_count:50,version:'12.1',validated:true});
function dom(file, handler) {
  const d = new JSDOM('<div id="contentBody"></div><div id="techdocMount"></div>', {url:'https://appliance.test/tech-doc',runScripts:'outside-only'});
  const w=d.window;
  w.AbortSignal=AbortSignal;
  const realTimeout=w.setTimeout.bind(w);
  w.setTimeout=(fn,ms,...args)=>realTimeout(fn,ms===400 ? 1 : ms===1000 ? 20 : ms,...args);
  w.NMS={utils:{esc:s=>String(s ?? '').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])),icons:{view:'',update:''}}};
  let opens=0;
  w.NMS.modal={open(title,body,foot){
    opens++;
    let el=w.document.getElementById('cmOverlay');
    if(!el){el=w.document.createElement('div');el.id='cmOverlay';w.document.body.append(el);}
    el.innerHTML='<button id="cmClose">x</button><div id="cmBody">'+body+'</div><div id="cmFoot">'+foot+'</div>';
    el.classList.add('open');el.querySelector('#cmClose').onclick=()=>el.classList.remove('open');return el;
  },close(){w.document.getElementById('cmOverlay')?.classList.remove('open');}};
  w.fetch=async(url,options)=>{const data=await handler(url,options);return {ok:!data.__error,status:data.__error ? 500 : 200,json:async()=>data};};
  w.eval(source(file));
  return {d,w,opens:()=>opens};
}

test('mount adopts running idle state without reopening modal; closing stays closed',async()=>{
  const h=dom('techdoc.js',u=>u.includes('/status') ? {ticket:1} : u.includes('/result') ? {...status,status:'done'} : {running:true,idle:true});
  try {
    await h.w.NMS.techdoc.mount();
    assert.match(h.w.document.getElementById('tdUpdate').textContent,/View progress/);
    assert.equal(h.opens(),0);
    h.w.document.getElementById('tdUpdate').click();assert.equal(h.opens(),1);
    h.w.document.getElementById('cmClose').click();
    await h.w.NMS.techdoc.mount();await wait(40);
    assert.equal(h.opens(),1);assert.equal(h.w.document.getElementById('cmOverlay').classList.contains('open'),false);
  } finally {h.w.NMS.techdoc.stop();h.d.window.close();}
});

test('late progress response cannot restart a stopped polling chain',async()=>{
  let resolve, calls=0;
  const h=dom('techdoc.js',u=>u.includes('/status') ? {ticket:1} : u.includes('/result') ? {...status,status:'done'} : (++calls===1 ? {running:true,stage:'fetch'} : new Promise(r=>{resolve=r;})));
  try {
    await h.w.NMS.techdoc.mount();await until(()=>resolve);
    h.w.NMS.techdoc.stop();resolve({running:true,stage:'fetch'});await wait(50);
    assert.equal(calls,2);
  } finally {h.d.window.close();}
});

test('out-of-order category replies cannot overwrite the selected category',async()=>{
  let releaseFirst;
  const h=dom('tech-doc.js',u=>{
    if(u==='/api/techdoc/status')return {ticket:1};
    if(u.includes('/documents?')){
      const q=new URL('https://a'+u).searchParams;
      if(q.get('docset')==='admin')return new Promise(r=>{releaseFirst=()=>r({ticket:2});});
      assert.equal(q.get('version'),'12.1');return {ticket:3};
    }
    if(u.endsWith('ticket=1'))return {...status,status:'done'};
    return {status:'done',documents:[doc(u.endsWith('ticket=2')?'old':'new')],total:1};
  });
  try {
    await until(()=>h.w.document.querySelectorAll('[data-category]').length===2);
    h.w.document.querySelectorAll('[data-category]')[0].click();await until(()=>releaseFirst);
    h.w.document.querySelectorAll('[data-category]')[1].click();
    await until(()=>h.w.document.querySelector('.tdp-doc-title')?.textContent==='new');
    releaseFirst();await wait(30);
    assert.equal(h.w.document.querySelector('.tdp-doc-title').textContent,'new');
  } finally {h.d.window.close();}
});

test('empty category shows an empty state, not an endless loading indicator',async()=>{
  const h=dom('tech-doc.js',u=>u==='/api/techdoc/status'?{ticket:1}:u.includes('/documents?')?{ticket:2}:u.endsWith('ticket=1')?{...status,status:'done'}:{status:'done',documents:[],total:0});
  try {
    await until(()=>h.w.document.querySelector('[data-category]'));
    h.w.document.querySelector('[data-category]').click();
    await until(()=>h.w.document.querySelector('.tdp-docs')?.textContent.includes('No matching documents.'));
    assert.equal(h.w.document.querySelector('.tdp-docs').getAttribute('aria-busy'),'false');
  } finally {h.d.window.close();}
});

test('status errors are visible and refresh remains available',async()=>{
  const h=dom('tech-doc.js',()=>({__error:true,error:'Migration required'}));
  try {
    await until(()=>h.w.document.querySelector('[role="alert"]'));
    assert.match(h.w.document.body.textContent,/Migration required/);
    assert.equal(h.w.document.getElementById('tdpRefresh').disabled,false);
  } finally {h.d.window.close();}
});

test('pagination requests bounded pages from the server',async()=>{
  const offsets=[];
  const h=dom('tech-doc.js',u=>{
    if(u==='/api/techdoc/status')return {ticket:1};
    if(u.includes('/documents?')){const q=new URL('https://a'+u).searchParams;offsets.push(Number(q.get('offset')));assert.equal(q.get('limit'),'100');return {ticket:2};}
    if(u.endsWith('ticket=1'))return {...status,status:'done'};
    return {status:'done',documents:Array.from({length:offsets.at(-1)?1:100},(_,i)=>doc('Topic'+i)),total:101};
  });
  try {
    await until(()=>h.w.document.querySelector('[data-category]'));
    h.w.document.querySelector('[data-category]').click();
    await until(()=>h.w.document.getElementById('tdpNext')?.disabled===false);
    h.w.document.getElementById('tdpNext').click();
    await until(()=>h.w.document.querySelectorAll('.tdp-doc').length===1);
    assert.deepEqual(offsets,[0,100]);
    assert.equal(h.w.document.getElementById('tdpNext').disabled,true);
  } finally {h.d.window.close();}
});

test('preview sends selected scope and does not reopen a dialog closed during start',async()=>{
  let releaseStart, sent;
  const h=dom('techdoc.js',(u,o)=>{
    if(u.includes('/status'))return {ticket:1};
    if(u.includes('/result'))return {...status,status:'done'};
    if(u.includes('/refresh')){sent=JSON.parse(o.body);return new Promise(r=>{releaseStart=()=>r({started:true});});}
    return {running:false,idle:true};
  });
  try {
    await h.w.NMS.techdoc.mount();h.w.document.getElementById('tdUpdate').click();
    h.w.document.getElementById('tdScope').value='ngfw';h.w.document.getElementById('tdDryRun').checked=true;
    h.w.document.getElementById('tdStart').click();await until(()=>releaseStart);
    h.w.document.getElementById('cmClose').click();releaseStart();await wait(40);
    assert.deepEqual(sent,{scope:'ngfw',dry_run:true});assert.equal(h.opens(),1);
    assert.equal(h.w.document.getElementById('cmOverlay').classList.contains('open'),false);
  } finally {h.w.NMS.techdoc.stop();h.d.window.close();}
});

test('search uses title/URL endpoint and preserves user focus',async()=>{
  let term;
  const h=dom('tech-doc.js',u=>{
    if(u==='/api/techdoc/status')return {ticket:1};
    if(u.includes('/documents?')){term=new URL('https://a'+u).searchParams.get('q');return {ticket:2};}
    if(u.endsWith('ticket=1'))return {...status,status:'done'};
    return {status:'done',documents:[doc('HIP')],total:1};
  });
  try {
    await until(()=>h.w.document.querySelector('[data-category]'));
    const input=h.w.document.getElementById('tdpSearch');input.focus();input.value='HIP';input.setSelectionRange(3,3);
    input.dispatchEvent(new h.w.Event('input'));
    await until(()=>h.w.document.querySelector('.tdp-doc-title')?.textContent==='HIP');
    assert.equal(term,'HIP');assert.equal(h.w.document.activeElement.id,'tdpSearch');
    assert.equal(h.w.document.activeElement.selectionStart,3);
  } finally {h.d.window.close();}
});
