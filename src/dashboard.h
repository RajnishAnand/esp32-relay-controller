#pragma once

namespace dashboard {

const char kHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Relay Control</title>
<style>
  * { box-sizing: border-box; }
  body { font-family: system-ui, sans-serif; background:#111; color:#eee; margin:0; padding:24px; max-width:520px; }
  .header { display:flex; justify-content:space-between; align-items:center; margin-bottom:20px; }
  h1 { font-size:1.3rem; margin:0; }
  .gear { background:none; border:none; color:#eee; font-size:1.4rem; cursor:pointer; padding:4px 8px; }
  .card { background:#1c1c1c; border-radius:10px; padding:16px 20px; margin-bottom:12px;
          display:flex; justify-content:space-between; align-items:center; }
  .name { font-size:1.05rem; word-break:break-word; }
  .status { font-size:0.85rem; opacity:0.6; margin-left:8px; }
  .sched { font-size:0.75rem; opacity:0.45; display:block; margin-top:2px; }
  .switch { position:relative; width:56px; height:30px; flex-shrink:0; margin-left:12px; }
  .switch input { opacity:0; width:0; height:0; }
  .slider { position:absolute; cursor:pointer; inset:0; background:#444; border-radius:30px; transition:0.2s; }
  .slider:before { content:""; position:absolute; height:22px; width:22px; left:4px; bottom:4px;
                    background:#eee; border-radius:50%; transition:0.2s; }
  input:checked + .slider { background:#2e8b57; }
  input:checked + .slider:before { transform:translateX(26px); }
  .allrow { display:flex; gap:10px; margin-top:20px; }
  button.action { flex:1; padding:10px; border:none; border-radius:8px; background:#333; color:#eee;
                  font-size:0.95rem; cursor:pointer; }
  button.action:active { background:#444; }
  #conn { font-size:0.8rem; opacity:0.5; margin-top:16px; }
  .overlay { display:none; position:fixed; inset:0; background:rgba(0,0,0,0.7);
             align-items:center; justify-content:center; overflow-y:auto; padding:20px; }
  .overlay.open { display:flex; }
  .panel { background:#1c1c1c; border-radius:12px; padding:20px; width:min(90vw, 380px); }
  .panel h2 { margin-top:0; font-size:1.1rem; }
  .relayBlock { border-top:1px solid #333; padding-top:12px; margin-top:12px; }
  .relayBlock:first-of-type { border-top:none; margin-top:0; }
  .panel label { display:block; font-size:0.85rem; opacity:0.7; margin:8px 0 4px; }
  .panel input[type=text], .panel input[type=password] { width:100%; padding:8px; border-radius:6px;
        border:1px solid #444; background:#111; color:#eee; font-size:0.95rem; }
  .schedRow { display:flex; gap:8px; align-items:center; margin-top:6px; }
  .schedRow input[type=time] { flex:1; padding:6px; border-radius:6px; border:1px solid #444;
                                background:#111; color:#eee; font-size:0.9rem; }
  .rowbtns { display:flex; gap:10px; margin-top:18px; }
  .banner { display:none; background:#1c2b22; border:1px solid #2e8b57; border-radius:10px;
            padding:12px 16px; margin-bottom:16px; font-size:0.9rem; }
  .banner.show { display:block; }
  .banner.error { background:#2b1c1c; border-color:#8b2e2e; }
  .banner button { margin-top:10px; width:100%; }
  .bar { height:6px; background:#333; border-radius:3px; margin-top:8px; overflow:hidden; }
  .bar > div { height:100%; background:#2e8b57; width:0%; transition:width 0.3s; }
  .fw { margin-top:16px; border-top:1px solid #333; padding-top:12px; font-size:0.85rem; }
  .fw button { width:100%; margin-top:8px; }
  #fwStatus, #loginMsg { opacity:0.7; margin-top:6px; font-size:0.85rem; }
</style>
</head>
<body>
<div class="header">
  <h1>Relay Dashboard</h1>
  <button class="gear" onclick="openSettings()" title="Configure">&#9881;</button>
</div>

<div id="updateBanner" class="banner"></div>
<div id="relays"></div>
<div class="allrow">
  <button class="action" onclick="setAll(true)">All ON</button>
  <button class="action" onclick="setAll(false)">All OFF</button>
</div>
<div id="conn">connecting...</div>

<div class="overlay" id="settingsOverlay">
  <div class="panel">
    <h2>Configure Switches</h2>
    <div id="configFields"></div>
    <div class="fw">
      <div id="fwInfo">Firmware</div>
      <div id="fwStatus"></div>
      <button class="action" onclick="checkUpdate()">Check for updates</button>
    </div>
    <div class="rowbtns">
      <button class="action" onclick="saveSettings()">Save</button>
      <button class="action" onclick="closeSettings()">Cancel</button>
    </div>
    <div class="rowbtns">
      <button class="action" onclick="forgetToken()">Forget access token</button>
    </div>
  </div>
</div>

<div class="overlay" id="loginOverlay">
  <div class="panel">
    <h2>Access token required</h2>
    <input type="password" id="tokenInput" placeholder="API token" autocomplete="off">
    <div id="loginMsg"></div>
    <div class="rowbtns"><button class="action" onclick="saveToken()">Unlock</button></div>
  </div>
</div>

<script>
const TOKEN_KEY = 'relayToken';
const BUSY = ['checking', 'downloading', 'verifying', 'rebooting'];
let token = loadToken();
let lastData = null;
let lastVer = null;
let updState = null;
let updFastTimer = null;

function loadToken() {
  try { return localStorage.getItem(TOKEN_KEY) || ''; } catch (e) { return ''; }
}
function storeToken(value) {
  try { if (value) localStorage.setItem(TOKEN_KEY, value); else localStorage.removeItem(TOKEN_KEY); } catch (e) {}
}
function esc(t) {
  return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}
function setConn(text) { document.getElementById('conn').textContent = text; }

function showLogin(message) {
  document.getElementById('loginMsg').textContent = message || '';
  document.getElementById('loginOverlay').classList.add('open');
}
function hideLogin() { document.getElementById('loginOverlay').classList.remove('open'); }

function saveToken() {
  token = document.getElementById('tokenInput').value.trim();
  storeToken(token);
  document.getElementById('tokenInput').value = '';
  showLogin('');
  fetchStatus();
  fetchUpdate();
}
function forgetToken() {
  token = '';
  storeToken('');
  closeSettings();
  showLogin('');
}

async function api(path, opts) {
  const o = opts || {};
  o.headers = Object.assign({ 'X-API-Key': token }, o.headers || {});
  const res = await fetch(path, o);
  if (res.status === 401) {
    showLogin(token ? 'Invalid token' : '');
    throw new Error('unauthorized');
  }
  if (res.status === 503) {
    setConn('device not provisioned');
    throw new Error('unprovisioned');
  }
  hideLogin();
  return res;
}

async function fetchStatus() {
  try {
    const res = await api('/api/status');
    lastData = await res.json();
    render(lastData);
    setConn('connected');
  } catch (e) {
    if (e.message !== 'unauthorized' && e.message !== 'unprovisioned') setConn('disconnected - retrying...');
  }
}

function render(data) {
  const container = document.getElementById('relays');
  container.innerHTML = '';
  data.relays.forEach((r, i) => {
    const card = document.createElement('div');
    card.className = 'card';
    const sched = r.schedEnabled ? `<span class="sched">Schedule: ${esc(r.onTime)} - ${esc(r.offTime)}</span>` : '';
    card.innerHTML = `
      <span class="name">${esc(r.name)}<span class="status">${r.state ? 'ON' : 'OFF'}</span>${sched}</span>
      <label class="switch">
        <input type="checkbox" ${r.state ? 'checked' : ''} onchange="toggle(${i}, this.checked)">
        <span class="slider"></span>
      </label>`;
    container.appendChild(card);
  });
}

async function toggle(idx, state) {
  try { await api(`/api/set?relay=${idx}&state=${state ? 1 : 0}`); } catch (e) {}
  fetchStatus();
}

async function setAll(state) {
  try { await api(`/api/all?state=${state ? 1 : 0}`); } catch (e) {}
  fetchStatus();
}

function openSettings() {
  if (!lastData) return;
  const fields = document.getElementById('configFields');
  fields.innerHTML = '';
  lastData.relays.forEach((r, i) => {
    const block = document.createElement('div');
    block.className = 'relayBlock';

    const nameLabel = document.createElement('label');
    nameLabel.textContent = `Switch ${i + 1} name`;
    const nameInput = document.createElement('input');
    nameInput.type = 'text';
    nameInput.maxLength = 24;
    nameInput.value = r.name;
    nameInput.id = `nameInput${i}`;

    const schedLabel = document.createElement('label');
    schedLabel.textContent = 'Daily schedule';
    const row = document.createElement('div');
    row.className = 'schedRow';
    const enable = document.createElement('input');
    enable.type = 'checkbox';
    enable.id = `schedEnable${i}`;
    enable.checked = r.schedEnabled;
    const on = document.createElement('input');
    on.type = 'time';
    on.id = `schedOn${i}`;
    on.value = r.onTime || '07:00';
    const off = document.createElement('input');
    off.type = 'time';
    off.id = `schedOff${i}`;
    off.value = r.offTime || '22:00';
    row.append(enable, on, off);

    block.append(nameLabel, nameInput, schedLabel, row);
    fields.appendChild(block);
  });
  document.getElementById('settingsOverlay').classList.add('open');
}

function closeSettings() {
  document.getElementById('settingsOverlay').classList.remove('open');
}

async function saveSettings() {
  try {
    for (let i = 0; i < 4; i++) {
      const name = document.getElementById(`nameInput${i}`).value.trim();
      if (name.length > 0) await api(`/api/rename?relay=${i}&name=${encodeURIComponent(name)}`);
      const enabled = document.getElementById(`schedEnable${i}`).checked;
      const on = document.getElementById(`schedOn${i}`).value || '07:00';
      const off = document.getElementById(`schedOff${i}`).value || '22:00';
      await api(`/api/schedule?relay=${i}&enabled=${enabled ? 1 : 0}&on=${on}&off=${off}`);
    }
  } catch (e) {}
  closeSettings();
  fetchStatus();
}

function scheduleFast() {
  if (!updFastTimer) updFastTimer = setTimeout(() => { updFastTimer = null; fetchUpdate(); }, 1000);
}

async function fetchUpdate() {
  try {
    const res = await api('/api/update/status');
    const u = await res.json();
    if (lastVer && u.current !== lastVer) { location.reload(); return; }
    lastVer = u.current;
    updState = u.state;
    renderUpdate(u);
  } catch (e) {}
  if (BUSY.includes(updState)) scheduleFast();
}

function renderUpdate(u) {
  const banner = document.getElementById('updateBanner');
  let html = '';
  let cls = 'banner';
  if (u.state === 'available') {
    html = `Update available: v${esc(u.latest)}<button class="action" onclick="installUpdate('${esc(u.latest)}')">Install v${esc(u.latest)}</button>`;
  } else if (u.state === 'downloading') {
    html = `Downloading v${esc(u.latest)}... ${Number(u.progress)}%<div class="bar"><div style="width:${Number(u.progress)}%"></div></div>`;
  } else if (u.state === 'verifying') {
    html = 'Verifying firmware...';
  } else if (u.state === 'rebooting') {
    html = 'Installed. Rebooting - relays will restore their saved state.';
  } else if (u.state === 'checking') {
    html = 'Checking for updates...';
  } else if (u.state === 'error') {
    html = 'Update problem: ' + esc(u.message);
    cls += ' error';
  }
  banner.className = html ? cls + ' show' : cls;
  banner.innerHTML = html;

  document.getElementById('fwInfo').textContent =
    'Firmware v' + u.current + (u.pendingConfirm ? ' (new firmware verifying itself...)' : '');
  let status = '';
  if (!u.configured) status = 'Updates are not configured in the firmware.';
  else if (u.state === 'up_to_date') status = u.message ? u.message : 'Up to date.';
  else if (u.state === 'available') status = 'v' + u.latest + ' is available.';
  else if (u.state === 'error') status = u.message;
  if (u.lastCheckSecondsAgo >= 0) {
    status += (status ? ' ' : '') + 'Last checked ' + Math.round(u.lastCheckSecondsAgo / 60) + ' min ago.';
  }
  document.getElementById('fwStatus').textContent = status;
}

async function checkUpdate() {
  document.getElementById('fwStatus').textContent = 'Checking...';
  try { await api('/api/update/check', { method: 'POST' }); } catch (e) {}
  updState = 'checking';
  fetchUpdate();
}

async function installUpdate(v) {
  const msg = 'Install v' + v + '?\n\nThe device will reboot. Relays will drop briefly, then restore their saved state. Do not cut power during the update.';
  if (!confirm(msg)) return;
  try { await api('/api/update/apply', { method: 'POST' }); } catch (e) {}
  updState = 'downloading';
  fetchUpdate();
}

fetchStatus();
fetchUpdate();
setInterval(fetchStatus, 4000);
setInterval(fetchUpdate, 4000);
</script>
</body>
</html>
)HTML";

}  // namespace dashboard
