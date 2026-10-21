const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const { JSDOM } = require('jsdom');

const header = fs.readFileSync(path.join(__dirname, '../../src/dashboard.h'), 'utf8');
const HTML = header.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const ADMIN = 'admin-token-0123456789';
const GUEST = 'guest-pass-1234';
const sleep = ms => new Promise(r => setTimeout(r, ms));

function makeDevice(overrides) {
  const d = Object.assign({
    calls: [],
    guestEnabled: true,
    locked: false,
    renameError: null,
    relays: [0, 1, 2, 3].map(i => ({ name: 'Relay ' + (i + 1), state: false, schedEnabled: false, onTime: '07:00', offTime: '22:00' })),
    wifi: { configured: true, ssid: 'home', connected: true, ip: '192.168.1.250', rssi: -55, apActive: false, change: { state: 'idle', message: '' } },
    scan: { state: 'done', networks: [{ ssid: 'Cafe', rssi: -60, secure: true, channel: 6 }, { ssid: 'Open', rssi: -70, secure: false, channel: 1 }] },
    update: { current: '1.0.0', configured: true, state: 'available', latest: '1.0.1', progress: 0, message: '', pendingConfirm: false, lastCheckSecondsAgo: 60 },
  }, overrides);
  const reply = (status, body) => {
    const text = typeof body === 'string' ? body : JSON.stringify(body);
    return { status, ok: status < 400, text: async () => text, json: async () => JSON.parse(text) };
  };
  d.fetch = async (url, opts) => {
    opts = opts || {};
    const key = (opts.headers || {})['X-API-Key'] || '';
    const u = new URL(url, 'http://device');
    d.calls.push({ path: u.pathname, search: u.search, method: opts.method || 'GET', key, body: opts.body ? Object.fromEntries(opts.body) : null });
    const isAdmin = key === ADMIN;
    const isGuest = d.guestEnabled && key === GUEST;
    if (!isAdmin && !isGuest) return reply(d.locked && key ? 429 : 401, 'denied');
    const p = u.pathname;
    if (p === '/api/status') return reply(200, { role: isAdmin ? 'admin' : 'guest', relays: d.relays });
    if (p === '/api/set') { d.relays[+u.searchParams.get('relay')].state = u.searchParams.get('state') === '1'; return reply(200, 'ok'); }
    if (p === '/api/all') { d.relays.forEach(r => { r.state = u.searchParams.get('state') === '1'; }); return reply(200, 'ok'); }
    if (!isAdmin) return reply(403, 'admin access required');
    if (p === '/api/rename') return d.renameError ? reply(400, d.renameError) : reply(200, 'ok');
    if (p === '/api/schedule') return reply(200, 'ok');
    if (p === '/api/wifi/status') return reply(200, d.wifi);
    if (p === '/api/wifi/scan') return reply(200, opts.method === 'POST' ? 'ok' : d.scan);
    if (p === '/api/wifi/connect') return reply(200, 'ok');
    if (p === '/api/guest') return reply(200, { enabled: d.guestEnabled });
    if (p === '/api/guest/password') return reply(200, 'ok');
    if (p === '/api/guest/disable') { d.guestEnabled = false; return reply(200, 'ok'); }
    if (p === '/api/update/status') return reply(200, d.update);
    if (p === '/api/update/check' || p === '/api/update/apply') return reply(200, 'ok');
    return reply(404, 'not found');
  };
  return d;
}

async function open(device, token) {
  const dom = new JSDOM(HTML, {
    url: 'http://localhost/', runScripts: 'dangerously', pretendToBeVisual: true,
    beforeParse(window) {
      window.fetch = device.fetch;
      window.confirm = () => true;
      const original = window.setTimeout.bind(window);
      window.setTimeout = (fn, ms, ...args) => original(fn, Math.min(ms || 0, 5), ...args);
      if (token) window.localStorage.setItem('relayToken', token);
    },
  });
  await sleep(60);
  return dom;
}

const $ = (dom, id) => dom.window.document.getElementById(id);
const paths = d => d.calls.map(c => c.path);

test('without a token the login box shows and no relays render', async () => {
  const d = makeDevice();
  const dom = await open(d);
  assert.strictEqual($(dom, 'login').hidden, false);
  assert.strictEqual($(dom, 'list').children.length, 0);
  assert.match($(dom, 'msg').textContent, /required/i);
  dom.window.close();
});

test('a wrong token shows an error and keeps the login box', async () => {
  const dom = await open(makeDevice(), 'nope');
  assert.strictEqual($(dom, 'login').hidden, false);
  assert.match($(dom, 'msg').textContent, /Invalid/);
  dom.window.close();
});

test('entering the token unlocks the dashboard and stores it', async () => {
  const d = makeDevice();
  const dom = await open(d);
  $(dom, 'tok').value = ADMIN;
  dom.window.saveTok();
  await sleep(60);
  assert.strictEqual($(dom, 'login').hidden, true);
  assert.strictEqual($(dom, 'list').children.length, 4);
  assert.strictEqual(dom.window.localStorage.getItem('relayToken'), ADMIN);
  dom.window.close();
});

test('admin sees four switches, the settings block and the role', async () => {
  const dom = await open(makeDevice(), ADMIN);
  assert.strictEqual($(dom, 'list').querySelectorAll('input[type=checkbox]').length, 4);
  assert.strictEqual($(dom, 'set').hidden, false);
  assert.match($(dom, 'who').textContent, /admin/);
  dom.window.close();
});

test('relay names are shown as text, never as markup', async () => {
  const d = makeDevice();
  d.relays[0].name = '<img src=x onerror="window.__pwned=1">';
  const dom = await open(d, ADMIN);
  assert.strictEqual($(dom, 'list').querySelectorAll('img').length, 0);
  assert.ok($(dom, 'list').textContent.includes('<img src=x'));
  assert.strictEqual(dom.window.__pwned, undefined);
  dom.window.close();
});

test('toggling a switch calls the API with the token header', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  const box = $(dom, 'list').querySelectorAll('input')[2];
  box.checked = true;
  box.dispatchEvent(new dom.window.Event('change'));
  await sleep(60);
  const call = d.calls.find(c => c.path === '/api/set');
  assert.strictEqual(call.search, '?relay=2&state=1');
  assert.strictEqual(call.key, ADMIN);
  assert.strictEqual(d.relays[2].state, true);
  dom.window.close();
});

test('all on and all off call /api/all', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  dom.window.setAll(1);
  await sleep(60);
  assert.ok(d.relays.every(r => r.state));
  dom.window.setAll(0);
  await sleep(60);
  assert.ok(d.relays.every(r => !r.state));
  dom.window.close();
});

test('a guest gets switches only and never touches admin endpoints', async () => {
  const d = makeDevice();
  const dom = await open(d, GUEST);
  assert.strictEqual($(dom, 'list').children.length, 4);
  assert.strictEqual($(dom, 'set').hidden, true);
  assert.match($(dom, 'who').textContent, /guest/);
  await dom.window.update();
  await sleep(40);
  assert.deepStrictEqual(paths(d).filter(p => p !== '/api/status'), []);
  dom.window.close();
});

test('a guest can still switch relays', async () => {
  const d = makeDevice();
  const dom = await open(d, GUEST);
  dom.window.setAll(1);
  await sleep(60);
  assert.ok(d.relays.every(r => r.state));
  dom.window.close();
});

test('opening settings builds the switch editor and loads wifi, guest and firmware status', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  $(dom, 'set').open = true;
  $(dom, 'set').dispatchEvent(new dom.window.Event('toggle'));
  await sleep(80);
  assert.strictEqual($(dom, 'cfg').querySelectorAll('fieldset').length, 4);
  assert.match($(dom, 'wst').textContent, /Connected to home \(192\.168\.1\.250, -55 dBm\)/);
  assert.match($(dom, 'gst').textContent, /Guest access is on/);
  assert.match($(dom, 'fw').textContent, /Firmware v1\.0\.0/);
  dom.window.close();
});

test('saving switches sends rename and schedule requests and reports validation errors', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  $(dom, 'set').open = true;
  $(dom, 'set').dispatchEvent(new dom.window.Event('toggle'));
  await sleep(60);
  $(dom, 'n1').value = 'Pump & Co';
  $(dom, 'e1').checked = true;
  $(dom, 'a1').value = '18:30';
  $(dom, 'b1').value = '23:00';
  await dom.window.saveCfg();
  const rename = d.calls.find(c => c.path === '/api/rename' && c.search.includes('relay=1'));
  assert.ok(rename.search.includes('name=Pump%20%26%20Co'));
  const sched = d.calls.find(c => c.path === '/api/schedule' && c.search.includes('relay=1'));
  assert.ok(sched.search.includes('enabled=1') && sched.search.includes('on=18:30') && sched.search.includes('off=23:00'));
  d.renameError = 'name must be 1-24 bytes';
  await dom.window.saveCfg();
  assert.match($(dom, 'msg').textContent, /name must be/);
  dom.window.close();
});

test('wifi scan fills the network list', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  await dom.window.wifiScan();
  const options = [...$(dom, 'wl').querySelectorAll('option')];
  assert.deepStrictEqual(options.map(o => o.value), ['Cafe', 'Open']);
  assert.match(options[1].label, /open/);
  dom.window.close();
});

test('wifi connect posts the password in the body, not the URL', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  $(dom, 'ws').value = 'Cafe';
  $(dom, 'wp').value = 'cafe-secret-1';
  await dom.window.wifiConnect();
  const call = d.calls.find(c => c.path === '/api/wifi/connect');
  assert.strictEqual(call.method, 'POST');
  assert.strictEqual(call.search, '');
  assert.deepStrictEqual(call.body, { ssid: 'Cafe', password: 'cafe-secret-1' });
  assert.strictEqual($(dom, 'wp').value, '');
  dom.window.close();
});

test('setting and disabling the guest password', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  $(dom, 'gp').value = 'new-guest-pass-9';
  await dom.window.setGuest();
  const set = d.calls.find(c => c.path === '/api/guest/password');
  assert.strictEqual(set.search, '');
  assert.deepStrictEqual(set.body, { password: 'new-guest-pass-9' });
  assert.strictEqual($(dom, 'gp').value, '');
  await dom.window.disableGuest();
  await sleep(40);
  assert.match($(dom, 'gst').textContent, /off/);
  dom.window.close();
});

test('an available update shows an install button that posts apply', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  await sleep(40);
  assert.strictEqual($(dom, 'upd').hidden, false);
  assert.match($(dom, 'upd').textContent, /Update v1\.0\.1 available/);
  $(dom, 'upd').querySelector('button').click();
  await sleep(60);
  assert.ok(d.calls.some(c => c.path === '/api/update/apply' && c.method === 'POST'));
  dom.window.close();
});

test('a failed update is shown as an error', async () => {
  const d = makeDevice();
  d.update = Object.assign({}, d.update, { state: 'error', message: 'Manifest signature INVALID - refusing update' });
  const dom = await open(d, ADMIN);
  await sleep(40);
  assert.strictEqual($(dom, 'upd').className, 'e');
  assert.match($(dom, 'upd').textContent, /INVALID/);
  dom.window.close();
});

test('too many failed guest attempts shows a wait message', async () => {
  const d = makeDevice({ locked: true });
  const dom = await open(d, 'some-wrong-guess');
  assert.match($(dom, 'msg').textContent, /Too many failed attempts/);
  dom.window.close();
});

test('signing out clears the token and the screen', async () => {
  const d = makeDevice();
  const dom = await open(d, ADMIN);
  dom.window.forget();
  assert.strictEqual(dom.window.localStorage.getItem('relayToken'), null);
  assert.strictEqual($(dom, 'list').children.length, 0);
  assert.strictEqual($(dom, 'login').hidden, false);
  assert.strictEqual($(dom, 'set').hidden, true);
  dom.window.close();
});
