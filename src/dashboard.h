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
fieldset{margin:.5em 0}input[type=text]{width:100%}
.e{color:#b00}small{color:#666}
</style>
</head>
<body>
<h1>Relays</h1>
<p id="msg"><small>connecting...</small></p>
<p id="login" hidden>Access token: <input id="tok" type="password" autocomplete="off"> <button onclick="saveTok()">Unlock</button></p>
<p id="upd" hidden></p>
<ul id="list"></ul>
<p><button onclick="setAll(1)">All on</button> <button onclick="setAll(0)">All off</button></p>
<details id="set">
<summary>Settings</summary>
<div id="cfg"></div>
<p><button onclick="saveCfg()">Save</button></p>
<p id="fw"></p>
<p><button onclick="check()">Check for updates</button> <button onclick="forget()">Forget token</button></p>
</details>
<script>
const $ = id => document.getElementById(id);
const BUSY = ['checking', 'downloading', 'verifying', 'rebooting'];
let tok = load(), data = null, ver = null, st = null, fast = null;

function load() { try { return localStorage.getItem('relayToken') || ''; } catch (e) { return ''; } }
function store(v) { try { v ? localStorage.setItem('relayToken', v) : localStorage.removeItem('relayToken'); } catch (e) {} }
function msg(t, err) { const m = $('msg'); m.textContent = t; m.className = err ? 'e' : ''; }
function h(tag, props, ...kids) { const e = document.createElement(tag); Object.assign(e, props); e.append(...kids); return e; }

async function api(path, method) {
  const r = await fetch(path, { method: method || 'GET', headers: { 'X-API-Key': tok } });
  if (r.status === 401) { $('login').hidden = false; msg(tok ? 'Invalid token' : 'Token required', 1); throw new Error('auth'); }
  if (r.status === 503) { msg('Device not provisioned', 1); throw new Error('auth'); }
  $('login').hidden = true;
  return r;
}

async function call(path) {
  const r = await api(path);
  if (!r.ok) throw new Error(await r.text());
}

async function refresh() {
  try { data = await (await api('/api/status')).json(); render(); msg(''); }
  catch (e) { if (e.message !== 'auth') msg('Disconnected, retrying...', 1); }
}

function render() {
  const ul = $('list');
  ul.textContent = '';
  data.relays.forEach((r, i) => {
    const c = h('input', { type: 'checkbox', checked: r.state, onchange: () => act('/api/set?relay=' + i + '&state=' + (c.checked ? 1 : 0)) });
    ul.append(h('li', {}, h('label', {}, c, ' ' + r.name + (r.schedEnabled ? ' (' + r.onTime + '-' + r.offTime + ')' : ''))));
  });
}

async function act(path) { try { await call(path); } catch (e) { if (e.message !== 'auth') msg(e.message, 1); } refresh(); }
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
};

async function saveCfg() {
  try {
    for (let i = 0; i < data.relays.length; i++) {
      const n = $('n' + i).value.trim();
      if (n) await call('/api/rename?relay=' + i + '&name=' + encodeURIComponent(n));
      await call('/api/schedule?relay=' + i + '&enabled=' + ($('e' + i).checked ? 1 : 0) +
                 '&on=' + ($('a' + i).value || '07:00') + '&off=' + ($('b' + i).value || '22:00'));
    }
    $('set').open = false;
  } catch (e) { if (e.message !== 'auth') msg(e.message, 1); }
  refresh();
}

function saveTok() { tok = $('tok').value.trim(); store(tok); $('tok').value = ''; refresh(); update(); }
function forget() { tok = ''; store(''); $('set').open = false; $('login').hidden = false; msg('Token required', 1); }

async function update() {
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
update();
setInterval(refresh, 4000);
setInterval(update, 4000);
</script>
</body>
</html>
)HTML";

}  // namespace dashboard
