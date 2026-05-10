#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Clock</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@500;700&display=swap" rel="stylesheet">
<style>
  *, *::before, *::after { margin:0; padding:0; box-sizing:border-box; }

  :root {
    --bg:          #0f2a5c;
    --card:        #0a1f47;
    --border:      #1a3a70;
    --text:        #cfe0ff;
    --text-dim:    #4d7ec4;
    --text-faint:  #2a4e8a;
    --accent:      #4d8ef0;
    --curl-bg:     #060e20;
    --curl-border: #132845;
    --btn-bg:      #1a2e5c;
    --btn-color:   #7ab0ff;
    --wifi-off:    #1a3a70;
    --slider-bg:   #0a1525;
  }

  [data-theme="light"] {
    --bg:          #dce8ff;
    --card:        #eef4ff;
    --border:      #a0c0f0;
    --text:        #0a1f47;
    --text-dim:    #2a5298;
    --text-faint:  #5b8fd4;
    --accent:      #1a5fc8;
    --curl-bg:     #e0ecff;
    --curl-border: #a0c0f0;
    --btn-bg:      #d0e4ff;
    --btn-color:   #1a3a8a;
    --wifi-off:    #b0c8f0;
    --slider-bg:   #c0d4f0;
  }

  body {
    background: var(--bg); color: var(--text);
    font-family: 'Inter', system-ui, sans-serif;
    min-height: 100vh; display: flex; flex-direction: column;
    align-items: center; justify-content: center;
    padding: 28px 16px; gap: 12px;
    transition: background 0.3s, color 0.3s;
  }

  .card {
    background: var(--card); border: 1px solid var(--border);
    border-radius: 18px; width: min(540px, 100%);
    transition: background 0.3s, border-color 0.3s;
  }

  .theme-btn {
    position: fixed; top: 14px; right: 14px;
    width: 40px; height: 40px; border-radius: 50%;
    background: var(--card); border: 1px solid var(--border);
    color: var(--text-dim); font-size: 18px; cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    z-index: 50; transition: background 0.2s, border-color 0.2s;
  }
  .theme-btn:hover { border-color: var(--accent); color: var(--accent); }

  .clock-card { padding: 30px 24px 24px; text-align: center; overflow: hidden; }
  #time {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(44px, 10vw, 82px); font-weight: 700;
    letter-spacing: -1px; line-height: 1; color: var(--text); white-space: nowrap;
  }
  .divider { border: none; border-top: 1px solid var(--border); margin: 18px 0 14px; }
  #date { font-size: clamp(15px, 3.5vw, 20px); font-weight: 600; color: var(--text-dim); letter-spacing: 3px; text-transform: uppercase; }
  #day  { font-size: clamp(12px, 2.5vw, 15px); font-weight: 500; color: var(--text-faint); letter-spacing: 5px; margin-top: 5px; text-transform: uppercase; }

  .stats-card { padding: 22px 24px; display: flex; flex-direction: column; gap: 18px; }
  .stats-row  { display: grid; grid-template-columns: 1fr 1fr; gap: 18px; }
  .stat       { display: flex; flex-direction: column; gap: 5px; min-width: 0; }

  .stat-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  .stat-value { font-family: 'JetBrains Mono', monospace; font-size: 17px; font-weight: 500; color: var(--text); line-height: 1.3; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .unit       { font-size: 13px; color: var(--text-dim); margin-left: 2px; }

  .req-badge { display: inline-flex; align-items: center; gap: 6px; }
  .req-dot   { width: 7px; height: 7px; border-radius: 50%; background: var(--accent); flex-shrink: 0; animation: pulse-dot 2s infinite; }
  @keyframes pulse-dot { 0%,100%{opacity:1;transform:scale(1)} 50%{opacity:.4;transform:scale(.7)} }

  .wifi-row  { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  .wifi-dots { display: inline-flex; gap: 3px; align-items: flex-end; }
  .wifi-dots span { display: inline-block; width: 6px; border-radius: 2px; background: var(--wifi-off); }
  .wifi-dots span.on { background: var(--accent); }

  .cpu-row      { display: flex; align-items: center; gap: 10px; }
  .cpu-mini-bar { flex: 1; height: 5px; background: var(--slider-bg); border-radius: 3px; overflow: hidden; max-width: 80px; }
  .cpu-mini-fill { height: 100%; border-radius: 3px; background: var(--accent); transition: width 1s ease; }
  .cpu-mini-fill.warn { background: #d97706; }
  .cpu-mini-fill.crit { background: #dc2626; }

  /* ── Яркость ── */
  .brightness-card { padding: 20px 24px; }
  .brightness-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; }
  .brightness-title  { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  .brightness-mode   { display: flex; gap: 6px; }

  .mode-btn {
    font-size: 11px; font-weight: 600; letter-spacing: 1px;
    padding: 4px 12px; border-radius: 6px; cursor: pointer;
    border: 1px solid var(--border); background: transparent;
    color: var(--text-faint); transition: all 0.2s; text-transform: uppercase;
    user-select: none;
  }
  .mode-btn.active { background: var(--accent); border-color: var(--accent); color: #fff; }
  .mode-btn:hover:not(.active):not(:disabled) { border-color: var(--accent); color: var(--text-dim); }

  .slider-row { display: flex; align-items: center; gap: 14px; }
  .brightness-icon { font-size: 14px; flex-shrink: 0; opacity: 0.6; }

  input[type="range"] {
    -webkit-appearance: none; appearance: none; flex: 1; height: 6px;
    background: var(--slider-bg); border-radius: 3px; outline: none; cursor: pointer;
    background-image: linear-gradient(var(--accent), var(--accent));
    background-size: 78% 100%; background-repeat: no-repeat;
    transition: opacity 0.2s;
  }
  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none; appearance: none;
    width: 18px; height: 18px; border-radius: 50%;
    background: #fff; border: 2px solid var(--accent);
    box-shadow: 0 1px 4px rgba(0,0,0,0.3); cursor: pointer;
    transition: transform 0.15s;
  }
  input[type="range"]::-webkit-slider-thumb:hover { transform: scale(1.2); }
  input[type="range"]::-moz-range-thumb {
    width: 18px; height: 18px; border-radius: 50%;
    background: #fff; border: 2px solid var(--accent); cursor: pointer;
  }
  input[type="range"][disabled] {
    opacity: 0.35; cursor: not-allowed; pointer-events: none;
  }
  .brightness-val { font-family: 'JetBrains Mono', monospace; font-size: 15px; font-weight: 600; color: var(--text); min-width: 40px; text-align: right; }

  /* ── Управление ── */
  .controls-card {
    padding: 16px 24px; display: flex;
    align-items: center; justify-content: space-between;
    gap: 12px; flex-wrap: wrap;
  }
  .controls-left  { display: flex; flex-direction: column; gap: 4px; }
  .controls-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  #uptime-label   { font-family: 'JetBrains Mono', monospace; font-size: 17px; color: var(--text); }

  .controls-right { display: flex; gap: 10px; flex-shrink: 0; }

  .btn-action {
    font-family: 'Inter', sans-serif; font-size: 12px; font-weight: 700;
    letter-spacing: 1px; padding: 10px 18px; border-radius: 9px;
    cursor: pointer; text-transform: uppercase; white-space: nowrap;
    border: 1px solid var(--border); background: var(--btn-bg);
    color: var(--btn-color); transition: background 0.2s, border-color 0.2s, color 0.2s;
  }
  .btn-action:disabled { opacity: 0.4; cursor: not-allowed; }

  /* Питание */
  .btn-power.on  { border-color: #16a34a; color: #16a34a; }
  .btn-power.off { border-color: #dc2626; color: #dc2626; }
  .btn-power.on:hover  { background: #16a34a; color: #fff; }
  .btn-power.off:hover { background: #dc2626; color: #fff; }

  /* Reboot */
  .btn-reboot:hover { background: #dc2626; border-color: #dc2626; color: #fff; }

  /* ── curl ── */
  .curl-card   { padding: 18px 24px; background: var(--curl-bg); border-color: var(--curl-border); }
  .curl-label  { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; margin-bottom: 12px; }
  .curl-line   { font-family: 'JetBrains Mono', monospace; font-size: 13px; line-height: 2.2; display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
  .curl-cmd  { color: #7ab0ff; }
  .curl-url  { color: #34d399; word-break: break-all; }
  .curl-note { color: var(--text-faint); font-size: 11px; }

  .status-bar   { display: flex; align-items: center; gap: 14px; font-size: 11px; font-weight: 500; color: var(--text-faint); letter-spacing: 1.5px; margin-top: 4px; }
  .ws-indicator { display: flex; align-items: center; gap: 5px; font-size: 10px; letter-spacing: 1px; text-transform: uppercase; }
  .ws-dot { width: 7px; height: 7px; border-radius: 50%; background: #6b7280; transition: background 0.3s; }
  .ws-dot.connected    { background: #16a34a; animation: pulse 2s infinite; }
  .ws-dot.disconnected { background: #dc2626; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.25} }

  .toast {
    position: fixed; bottom: 24px; left: 50%; transform: translateX(-50%);
    background: var(--card); border: 1px solid var(--border);
    color: var(--text); font-size: 14px; font-weight: 500;
    padding: 12px 24px; border-radius: 10px;
    opacity: 0; transition: opacity 0.3s;
    pointer-events: none; white-space: nowrap; z-index: 100;
  }
  .toast.show { opacity: 1; }
</style>
</head>
<body>

<button class="theme-btn" id="theme-btn" onclick="toggleTheme()" title="Toggle theme">🌙</button>

<div class="card clock-card">
  <div id="time">--:--:--</div>
  <hr class="divider">
  <div id="date">-- --- ----</div>
  <div id="day">---------</div>
</div>

<div class="card stats-card">
  <div class="stats-row">
    <div class="stat">
      <div class="stat-label">Temperature</div>
      <div class="stat-value"><span id="temp">—</span><span class="unit">°C</span></div>
    </div>
    <div class="stat">
      <div class="stat-label">CPU Load</div>
      <div class="cpu-row">
        <div class="stat-value"><span id="cpu-val">—</span><span class="unit">%</span></div>
        <div class="cpu-mini-bar"><div class="cpu-mini-fill" id="cpu-bar" style="width:0%"></div></div>
      </div>
    </div>
  </div>
  <div class="stats-row">
    <div class="stat">
      <div class="stat-label">WiFi SSID</div>
      <div class="stat-value" id="ssid">—</div>
    </div>
    <div class="stat">
      <div class="stat-label">IP Address</div>
      <div class="stat-value" id="ip">—</div>
    </div>
  </div>
  <div class="stat">
    <div class="stat-label">WiFi Signal</div>
    <div class="wifi-row">
      <div class="stat-value"><span id="rssi-val">—</span><span class="unit">dBm</span></div>
      <span class="wifi-dots">
        <span id="d1" style="height:5px"></span>
        <span id="d2" style="height:8px"></span>
        <span id="d3" style="height:12px"></span>
        <span id="d4" style="height:16px"></span>
      </span>
    </div>
  </div>
  <div class="stat">
    <div class="stat-label">RAM</div>
    <div class="stat-value" id="ram-txt">—</div>
  </div>
  <div class="stat">
    <div class="stat-label">Requests Since Boot</div>
    <div class="stat-value">
      <div class="req-badge"><span class="req-dot"></span><span id="req-count">—</span></div>
    </div>
  </div>
</div>

<!-- Яркость -->
<div class="card brightness-card">
  <div class="brightness-header">
    <div class="brightness-title">Display Brightness</div>
    <div class="brightness-mode">
      <button class="mode-btn active" id="btn-auto"   onclick="setBrightnessMode('auto')">Auto</button>
      <button class="mode-btn"        id="btn-manual" onclick="setBrightnessMode('manual')">Manual</button>
    </div>
  </div>
  <div class="slider-row">
    <span class="brightness-icon">🌑</span>
    <input type="range" id="brightness-slider"
           min="0" max="100" value="78" disabled
           oninput="onSliderInput(this.value)"
           onchange="sendBrightness(this.value)">
    <span class="brightness-icon">☀️</span>
    <span class="brightness-val"><span id="brightness-pct">—</span>%</span>
  </div>
</div>

<!-- Управление -->
<div class="card controls-card">
  <div class="controls-left">
    <div class="controls-label">Uptime</div>
    <div id="uptime-label">—</div>
  </div>
  <div class="controls-right">
    <button class="btn-action btn-power on" id="btn-power" onclick="togglePower()">⏻ Display On</button>
    <button class="btn-action btn-reboot"   id="btn-reboot" onclick="reboot()">Reboot</button>
  </div>
</div>

<!-- curl -->
<div class="card curl-card">
  <div class="curl-label">API · curl examples</div>
  <div class="curl-line"><span class="curl-cmd">curl</span><span class="curl-url" id="curl-time">http://&lt;IP&gt;/api/time</span></div>
  <div class="curl-line"><span class="curl-cmd">curl</span><span class="curl-url" id="curl-stats">http://&lt;IP&gt;/api/stats</span></div>
  <div class="curl-line"><span class="curl-cmd">curl -X POST</span><span class="curl-url" id="curl-bright">http://&lt;IP&gt;/api/brightness -d "value=80"</span></div>
  <div class="curl-line"><span class="curl-cmd">curl -X POST</span><span class="curl-url" id="curl-power">http://&lt;IP&gt;/api/power -d "on=0"</span></div>
  <div class="curl-line curl-note"># brightness auto: -d "auto=1" &nbsp;|&nbsp; power on: -d "on=1"</div>
</div>

<div class="status-bar">
  <span>ESP32-C3 SUPER MINI</span>
  <span class="ws-indicator">
    <span class="ws-dot" id="ws-dot"></span>
    <span id="ws-label">CONNECTING</span>
  </span>
</div>

<div class="toast" id="toast"></div>

<script>
  // ── Тема ─────────────────────────────────────────────
  applyTheme(localStorage.getItem('theme') || 'dark');
  function applyTheme(t) {
    document.documentElement.setAttribute('data-theme', t);
    document.getElementById('theme-btn').textContent = t==='dark'?'🌙':'☀️';
  }
  function toggleTheme() {
    const next = document.documentElement.getAttribute('data-theme')==='dark'?'light':'dark';
    applyTheme(next); localStorage.setItem('theme', next);
  }

  // ── Состояние ────────────────────────────────────────
  let isManual    = false;
  let isDisplayOn = true;
  let isDragging  = false;

  // ── Слайдер ──────────────────────────────────────────
  const slider = document.getElementById('brightness-slider');

  slider.addEventListener('mousedown', () => { isDragging = true;  });
  slider.addEventListener('touchstart', () => { isDragging = true;  });
  slider.addEventListener('mouseup',   () => { isDragging = false; });
  slider.addEventListener('touchend',  () => { isDragging = false; });

  function onSliderInput(val) {
    val = parseInt(val);
    slider.style.backgroundSize = val + '% 100%';
    document.getElementById('brightness-pct').textContent = val;
  }

  function sendBrightness(val) {
    fetch('/api/brightness', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'value=' + val
    });
  }

  // ── Режим яркости ────────────────────────────────────
  function setBrightnessMode(mode) {
    // Немедленно обновляем UI — не ждём WS
    const manual = mode === 'manual';
    isManual = manual;
    _applyModeUI(manual);

    if (manual) {
      // Ничего не отправляем — пользователь сам потянет слайдер
    } else {
      fetch('/api/brightness', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'auto=1'
      }).then(() => showToast('Auto brightness enabled'));
    }
  }

  function _applyModeUI(manual) {
    document.getElementById('btn-auto').classList.toggle('active',  !manual);
    document.getElementById('btn-manual').classList.toggle('active', manual);
    // disabled через атрибут — надёжнее чем .disabled property
    if (manual) {
      slider.removeAttribute('disabled');
    } else {
      slider.setAttribute('disabled', '');
    }
  }

  // ── Питание ──────────────────────────────────────────
  function togglePower() {
    const newState = !isDisplayOn;
    fetch('/api/power', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'on=' + (newState ? '1' : '0')
    });
    // UI обновится через WS broadcast от сервера
  }

  function _applyPowerUI(on) {
    isDisplayOn = on;
    const btn = document.getElementById('btn-power');
    btn.textContent = on ? '⏻ Display On' : '⏻ Display Off';
    btn.className   = 'btn-action btn-power ' + (on ? 'on' : 'off');
  }

  // ── WebSocket ─────────────────────────────────────────
  let ws, wsReconnectTimer;
  function wsConnect() {
    ws = new WebSocket(`ws://${location.hostname}:81/`);
    ws.onopen    = () => { setWsStatus(true);  clearTimeout(wsReconnectTimer); };
    ws.onmessage = (e) => { try { updateUI(JSON.parse(e.data)); } catch(_) {} };
    ws.onclose   = () => { setWsStatus(false); wsReconnectTimer = setTimeout(wsConnect, 3000); };
    ws.onerror   = () => ws.close();
  }
  function setWsStatus(ok) {
    document.getElementById('ws-dot').className    = 'ws-dot '+(ok?'connected':'disconnected');
    document.getElementById('ws-label').textContent = ok?'LIVE':'OFFLINE';
  }

  // ── UI update ─────────────────────────────────────────
  function updateUI(d) {
    document.getElementById('time').textContent         = d.time;
    document.getElementById('date').textContent         = d.date;
    document.getElementById('day').textContent          = d.day;
    document.getElementById('uptime-label').textContent = d.uptime;
    document.getElementById('ssid').textContent         = d.ssid;
    document.getElementById('ip').textContent           = d.ip;
    document.getElementById('temp').textContent         = d.temp;
    document.getElementById('rssi-val').textContent     = d.rssi;
    document.getElementById('cpu-val').textContent      = d.cpu;
    document.getElementById('req-count').textContent    = d.requests.toLocaleString();
    document.getElementById('curl-time').textContent    = 'http://'+d.ip+'/api/time';
    document.getElementById('curl-stats').textContent   = 'http://'+d.ip+'/api/stats';
    document.getElementById('curl-bright').textContent  = 'http://'+d.ip+'/api/brightness -d "value=80"';
    document.getElementById('curl-power').textContent   = 'http://'+d.ip+'/api/power -d "on=0"';

    // Слайдер — только если не тянем и режим авто
    if (!isDragging && !isManual) {
      slider.value = d.brightness_pct;
      slider.style.backgroundSize = d.brightness_pct + '% 100%';
      document.getElementById('brightness-pct').textContent = d.brightness_pct;
    }

    // Синхронизируем режим только если он поменялся на сервере
    const serverManual = d.brightness_manual === true;
    if (serverManual !== isManual) {
      isManual = serverManual;
      _applyModeUI(serverManual);
    }

    // Питание
    if (d.display_on !== isDisplayOn) {
      _applyPowerUI(d.display_on);
    }

    // CPU
    const cpuEl = document.getElementById('cpu-bar');
    cpuEl.style.width = d.cpu + '%';
    cpuEl.className = 'cpu-mini-fill'+(d.cpu>80?' crit':d.cpu>50?' warn':'');

    // WiFi
    const lvl = d.rssi>=-50?4:d.rssi>=-60?3:d.rssi>=-70?2:1;
    for(let i=1;i<=4;i++)
      document.getElementById('d'+i).className = i<=lvl?'on':'';

    // RAM
    const used = d.ram_total - d.ram_free;
    const pct  = Math.round(used*100/d.ram_total);
    document.getElementById('ram-txt').textContent =
      (d.ram_free/1024).toFixed(1)+' KB free / '+
      (d.ram_total/1024).toFixed(1)+' KB · '+pct+'% used';
  }

  // ── Reboot ────────────────────────────────────────────
  function showToast(msg, ms=2500) {
    const t = document.getElementById('toast');
    t.textContent = msg; t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), ms);
  }
  async function reboot() {
    if (!confirm('Reboot the clock?')) return;
    const btn = document.getElementById('btn-reboot');
    btn.disabled = true; btn.textContent = 'Rebooting…';
    try { await fetch('/api/reboot', { method: 'POST' }); } catch(e) {}
    showToast('Rebooting… reconnecting in 6s', 6000);
    setTimeout(() => { btn.disabled=false; btn.textContent='Reboot'; }, 7000);
  }

  wsConnect();
</script>
</body>
</html>
)rawliteral";
