#pragma once

namespace dashboard {

const char kHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relays</title>
<style>
body{font:16px sans-serif;max-width:30em;margin:1em auto;padding:0 1em}
ul{list-style:none;padding:0}li{margin:.6em 0}
fieldset{margin:.5em 0}input[type=text],input[type=password]{width:100%;box-sizing:border-box}
h3{margin:1em 0 .3em}.e{color:#b00}small{color:#666}
</style>
</head>
<body>
<h1>Relays <small id="who"></small></h1>
<p id="msg"><small>connecting...</small></p>
<p id="login" hidden>Access token or guest password: <input id="tok" type="password" autocomplete="off"> <button onclick="saveTok()">Unlock</button></p>
<p id="upd" hidden></p>
<ul id="list"></ul>
<p><button onclick="setAll(1)">All on</button> <button onclick="setAll(0)">All off</button></p>
<details id="set" hidden>
<summary>Settings</summary>
<h3>Switches</h3>
<div id="cfg"></div>
<p><button onclick="saveCfg()">Save switches</button></p>
<h3>WiFi</h3>
<p id="wst"></p>
<p><button onclick="wifiScan()">Scan for networks</button></p>
<p><input id="ws" type="text" list="wl" placeholder="Network name" maxlength="32"><datalist id="wl"></datalist></p>
<p><input id="wp" type="password" placeholder="Password (empty if open)" maxlength="63" autocomplete="off"></p>
<p><button onclick="wifiConnect()">Connect</button></p>
<h3>Guest access</h3>
<p id="gst"></p>
<p><input id="gp" type="password" placeholder="New guest password (8+ characters)" maxlength="64" autocomplete="off"></p>
<p><button onclick="setGuest()">Set guest password</button> <button onclick="disableGuest()">Disable</button></p>
<h3>Firmware</h3>
<p id="fw"></p>
<p><button onclick="check()">Check for updates</button></p>
</details>
<p><button onclick="forget()">Sign out</button></p>
<script>
const $ = id => document.getElementById(id);
const BUSY = ['checking', 'downloading', 'verifying', 'rebooting'];
let tok = load(), data = null, role = null, ver = null, st = null, fast = null;

function load() { try { return localStorage.getItem('relayToken') || ''; } catch (e) { return ''; } }
function store(v) { try { v ? localStorage.setItem('relayToken', v) : localStorage.removeItem('relayToken'); } catch (e) {} }
function msg(t, err) { const m = $('msg'); m.textContent = t; m.className = err ? 'e' : ''; }
function h(tag, props, ...kids) { const e = document.createElement(tag); Object.assign(e, props); e.append(...kids); return e; }
function fail(e) { if (e.message !== 'auth') msg(e.message, 1); }

async function api(path, method, body) {
  const r = await fetch(path, { method: method || 'GET', headers: { 'X-API-Key': tok }, body: body ? new URLSearchParams(body) : undefined });
  if (r.status === 401) { $('login').hidden = false; msg(tok ? 'Invalid token or password' : 'Token or password required', 1); throw new Error('auth'); }
  if (r.status === 429) { msg('Too many failed attempts. Wait a minute.', 1); throw new Error('auth'); }
  if (r.status === 503) { msg('Device not provisioned', 1); throw new Error('auth'); }
  $('login').hidden = true;
  return r;
}

async function call(path, method, body) {
  const r = await api(path, method, body);
  if (!r.ok) throw new Error(await r.text());
  return r;
}

async function refresh() {
  try {
    data = await (await api('/api/status')).json();
    role = data.role;
    $('who').textContent = '(' + role + ')';
    $('set').hidden = role !== 'admin';
    render();
    msg('');
    if (role === 'admin' && ver === null) update();
  } catch (e) { if (e.message !== 'auth') msg('Disconnected, retrying...', 1); }
}

function render() {
  const ul = $('list');
  ul.textContent = '';
  data.relays.forEach((r, i) => {
    const c = h('input', { type: 'checkbox', checked: r.state, onchange: () => act('/api/set?relay=' + i + '&state=' + (c.checked ? 1 : 0)) });
    ul.append(h('li', {}, h('label', {}, c, ' ' + r.name + (r.schedEnabled ? ' (' + r.onTime + '-' + r.offTime + ')' : ''))));
  });
}

async function act(path) { try { await call(path); } catch (e) { fail(e); } refresh(); }
function setAll(on) { act('/api/all?state=' + on); }

$('set').ontoggle = function () {
  if (!this.open || !data) return;
  const box = $('cfg');
  box.textContent = '';
  data.relays.forEach((r, i) => box.append(h('fieldset', {},
    h('legend', {}, 'Switch ' + (i + 1)),
    h('input', { type: 'text', id: 'n' + i, value: r.name, maxLength: 24 }),
    h('label', {}, h('input', { type: 'checkbox', id: 'e' + i, checked: r.schedEnabled }), ' Daily schedule '),
    h('input', { type: 'time', id: 'a' + i, value: r.onTime }), ' to ',
    h('input', { type: 'time', id: 'b' + i, value: r.offTime }))));
  wifiStatus();
  guestStatus();
  update();
};

async function saveCfg() {
  try {
    for (let i = 0; i < data.relays.length; i++) {
      const n = $('n' + i).value.trim();
      if (n) await call('/api/rename?relay=' + i + '&name=' + encodeURIComponent(n));
      await call('/api/schedule?relay=' + i + '&enabled=' + ($('e' + i).checked ? 1 : 0) +
                 '&on=' + ($('a' + i).value || '07:00') + '&off=' + ($('b' + i).value || '22:00'));
    }
    msg('Saved');
  } catch (e) { fail(e); }
  refresh();
}

async function wifiStatus() {
  try {
    const w = await (await call('/api/wifi/status')).json();
    let t = w.connected ? 'Connected to ' + w.ssid + ' (' + w.ip + ', ' + w.rssi + ' dBm)'
          : w.configured ? 'Not connected. Saved network: ' + w.ssid : 'No network saved';
    if (w.apActive) t += '. Access point is on.';
    if (w.change.state === 'trying') t += '. Switching networks...';
    if (w.change.message) t += '. ' + w.change.message;
    $('wst').textContent = t;
  } catch (e) { fail(e); }
}

async function wifiScan() {
  try {
    await call('/api/wifi/scan', 'POST');
    msg('Scanning...');
    for (let i = 0; i < 20; i++) {
      await new Promise(r => setTimeout(r, 1500));
      const s = await (await call('/api/wifi/scan')).json();
      if (s.state === 'done') {
        const dl = $('wl');
        dl.textContent = '';
        s.networks.forEach(n => dl.append(h('option', { value: n.ssid, label: n.ssid + ' (' + n.rssi + ' dBm' + (n.secure ? '' : ', open') + ')' })));
        msg(s.networks.length + ' networks found. Pick one in the name box.');
        return;
      }
      if (s.state === 'idle') break;
    }
    msg('Scan failed', 1);
  } catch (e) { fail(e); }
}

async function wifiConnect() {
  const ssid = $('ws').value;
  if (!ssid) return;
  if (!confirm('Switch to "' + ssid + '"? The device will leave its current network and may become unreachable at this address. If the new network fails, it returns to the previous one.')) return;
  try {
    await call('/api/wifi/connect', 'POST', { ssid: ssid, password: $('wp').value });
    $('wp').value = '';
    msg('Switching networks. If this page stops responding, reopen it at the device address on the new network (normally x.x.x.250).');
  } catch (e) { fail(e); }
  setTimeout(wifiStatus, 5000);
}

async function guestStatus() {
  try { $('gst').textContent = 'Guest access is ' + ((await (await call('/api/guest')).json()).enabled ? 'on' : 'off') + '.'; }
  catch (e) { fail(e); }
}

async function setGuest() {
  try { await call('/api/guest/password', 'POST', { password: $('gp').value }); $('gp').value = ''; msg('Guest password set'); }
  catch (e) { fail(e); }
  guestStatus();
}

async function disableGuest() {
  try { await call('/api/guest/disable', 'POST'); msg('Guest access disabled'); } catch (e) { fail(e); }
  guestStatus();
}

function saveTok() { tok = $('tok').value.trim(); store(tok); $('tok').value = ''; ver = null; refresh(); }
function forget() { tok = ''; store(''); role = null; ver = null; $('set').open = false; $('set').hidden = true; $('list').textContent = ''; $('who').textContent = ''; $('login').hidden = false; msg('Signed out'); }

async function update() {
  if (role !== 'admin') return;
  try {
    const u = await (await api('/api/update/status')).json();
    if (ver && u.current !== ver) return location.reload();
    ver = u.current;
    st = u.state;
    showUpdate(u);
  } catch (e) {}
  if (BUSY.includes(st) && !fast) fast = setTimeout(() => { fast = null; update(); }, 1000);
}

function showUpdate(u) {
  const b = $('upd');
  b.textContent = '';
  b.className = '';
  if (u.state === 'available') b.append('Update v' + u.latest + ' available. ', h('button', { onclick: () => install(u.latest) }, 'Install'));
  else if (u.state === 'downloading') b.append('Downloading v' + u.latest + ' ', h('progress', { max: 100, value: u.progress }), ' ' + u.progress + '%');
  else if (u.state === 'verifying') b.append('Verifying firmware...');
  else if (u.state === 'rebooting') b.append('Installed. Rebooting...');
  else if (u.state === 'checking') b.append('Checking for updates...');
  else if (u.state === 'error') { b.className = 'e'; b.append('Update problem: ' + u.message); }
  b.hidden = !b.firstChild;
  let t = 'Firmware v' + u.current + (u.pendingConfirm ? ' (verifying new firmware)' : '');
  if (!u.configured) t += '. Updates not configured.';
  else if (u.state === 'up_to_date') t += '. ' + (u.message || 'Up to date.');
  if (u.lastCheckSecondsAgo >= 0) t += ' Checked ' + Math.round(u.lastCheckSecondsAgo / 60) + ' min ago.';
  $('fw').textContent = t;
}

async function check() { try { await api('/api/update/check', 'POST'); } catch (e) {} st = 'checking'; update(); }

async function install(v) {
  if (!confirm('Install v' + v + '? The device will reboot and relays will drop briefly. Do not cut power.')) return;
  try { await api('/api/update/apply', 'POST'); } catch (e) {}
  st = 'downloading';
  update();
}

refresh();
setInterval(refresh, 4000);
setInterval(update, 4000);
</script>
</body>
</html>
)HTML";

}  // namespace dashboard
