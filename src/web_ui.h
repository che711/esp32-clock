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

  /* ── Часы ── */
  .clock-card { padding: 30px 24px 24px; text-align: center; overflow: hidden; }
  #time {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(44px, 10vw, 82px); font-weight: 700;
    letter-spacing: -1px; line-height: 1; color: var(--text); white-space: nowrap;
  }
  .divider { border: none; border-top: 1px solid var(--border); margin: 18px 0 14px; }
  #date { font-size: clamp(15px, 3.5vw, 20px); font-weight: 600; color: var(--text-dim); letter-spacing: 3px; text-transform: uppercase; }
  #day  { font-size: clamp(12px, 2.5vw, 15px); font-weight: 500; color: var(--text-faint); letter-spacing: 5px; margin-top: 5px; text-transform: uppercase; }

  /* ── Статистика ── */
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

  .cpu-row       { display: flex; align-items: center; gap: 10px; }
  .cpu-mini-bar  { flex: 1; height: 5px; background: var(--slider-bg); border-radius: 3px; overflow: hidden; max-width: 80px; }
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
    padding: 5px 14px; border-radius: 6px;
    border: 1px solid var(--border);
    background: transparent; color: var(--text-faint);
    text-transform: uppercase; cursor: pointer;
    transition: background 0.15s, border-color 0.15s, color 0.15s;
    user-select: none; -webkit-user-select: none;
  }
  .mode-btn.active {
    background: var(--accent); border-color: var(--accent); color: #fff;
    pointer-events: none;
  }
  .mode-btn:not(.active):hover { border-color: var(--accent); color: var(--text-dim); }

  .slider-row { display: flex; align-items: center; gap: 14px; }
  .brightness-icon { font-size: 14px; flex-shrink: 0; opacity: 0.6; }

  input[type="range"] {
    -webkit-appearance: none; appearance: none;
    flex: 1; height: 6px; border-radius: 3px; outline: none;
    background: linear-gradient(var(--accent), var(--accent)) 0/78% 100% no-repeat,
                var(--slider-bg);
    cursor: pointer; transition: opacity 0.2s;
  }
  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none; appearance: none;
    width: 18px; height: 18px; border-radius: 50%;
    background: #fff; border: 2px solid var(--accent);
    box-shadow: 0 1px 4px rgba(0,0,0,0.3);
    cursor: pointer; transition: transform 0.15s;
  }
  input[type="range"]::-webkit-slider-thumb:hover { transform: scale(1.2); }
  input[type="range"]::-moz-range-thumb {
    width: 18px; height: 18px; border-radius: 50%;
    background: #fff; border: 2px solid var(--accent); cursor: pointer;
  }
  input[type="range"].disabled-slider {
    opacity: 0.35; pointer-events: none; cursor: not-allowed;
  }
  .brightness-val { font-family: 'JetBrains Mono', monospace; font-size: 15px; font-weight: 600; color: var(--text); min-width: 40px; text-align: right; }

  /* ── Управление ── */
  .controls-card {
    padding: 16px 24px; display: flex;
    align-items: center; justify-content: space-between; gap: 12px; flex-wrap: wrap;
  }
  .controls-left  { display: flex; flex-direction: column; gap: 4px; }
  .controls-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  #uptime-label   { font-family: 'JetBrains Mono', monospace; font-size: 17px; color: var(--text); }
  .controls-right { display: flex; gap: 10px; flex-shrink: 0; }

  .btn-action {
    font-family: 'Inter', sans-serif; font-size: 12px; font-weight: 700;
    letter-spacing: 1px; padding: 10px 18px; border-radius: 9px;
    cursor: pointer; text-transform: uppercase; white-space: nowrap;
    border: 1px solid var(--border); background: var(--btn-bg); color: var(--btn-color);
    transition: background 0.2s, border-color 0.2s, color 0.2s;
  }
  .btn-action:disabled { opacity: 0.4; cursor: not-allowed; }

  .btn-power.on  { border-color: #16a34a; color: #16a34a; }
  .btn-power.off { border-color: #dc2626; color: #dc2626; }
  .btn-power.on:hover:not(:disabled)  { background: #16a34a; color: #fff; }
  .btn-power.off:hover:not(:disabled) { background: #dc2626; color: #fff; }
  .btn-reboot:hover:not(:disabled) { background: #dc2626; border-color: #dc2626; color: #fff; }

  /* ── curl ── */
  .curl-card  { padding: 18px 24px; background: var(--curl-bg); border-color: var(--curl-border); }
  .curl-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; margin-bottom: 12px; }
  .curl-line  { font-family: 'JetBrains Mono', monospace; font-size: 13px; color: #79c0ff; flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; user-select: all; }
  .curl-note  { color: var(--text-faint); font-size: 11px; display: block; margin-top: 4px; }

  /* ── Статус ── */
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

  /* ── curl copy buttons ── */
  .curl-row { display: flex; align-items: center; gap: 10px; }
  .curl-row .curl-line { flex: 1; margin: 0; }
  .curl-copy {
    flex-shrink: 0; padding: 3px 10px; border-radius: 5px;
    font-size: 10px; font-weight: 700; letter-spacing: 0.8px;
    border: 1px solid var(--curl-border); background: transparent;
    color: var(--text-faint); cursor: pointer; text-transform: uppercase;
    font-family: 'Inter', sans-serif;
    transition: border-color 0.15s, color 0.15s, background 0.15s;
  }
  .curl-copy:hover { border-color: var(--accent); color: var(--accent); }
  .curl-copy.copied { border-color: #16a34a; color: #16a34a; }

  /* ── RAM bar ── */
  .ram-row { display: flex; flex-direction: column; gap: 5px; }
  .ram-bar-wrap { height: 4px; background: var(--slider-bg); border-radius: 2px; overflow: hidden; max-width: 200px; }
  .ram-bar-fill { height: 100%; border-radius: 2px; background: var(--accent); transition: width 1s ease; }
  .ram-bar-fill.warn { background: #d97706; }
  .ram-bar-fill.crit { background: #dc2626; }

  /* ── Chip info card ── */
  .chip-card { padding: 18px 24px; }
  .chip-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; margin-bottom: 14px; }
  .chip-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px 20px; }
  .chip-item { display: flex; flex-direction: column; gap: 2px; }
  .chip-key  { font-size: 10px; font-weight: 700; color: var(--text-faint); letter-spacing: 1px; text-transform: uppercase; }
  .chip-val  { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 500; color: var(--text); }
</style>
</head>
<body>

<button class="theme-btn" id="theme-btn" onclick="toggleTheme()" title="Toggle theme">🌙</button>

<!-- Часы -->
<div class="card clock-card">
  <div id="time">--:--:--</div>
  <hr class="divider">
  <div id="date">-- --- ----</div>
  <div id="day">---------</div>
</div>

<!-- Статистика -->
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
    <div class="ram-row">
      <div class="stat-value" id="ram-txt">—</div>
      <div class="ram-bar-wrap"><div class="ram-bar-fill" id="ram-bar" style="width:0%"></div></div>
    </div>
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
           min="0" max="100" value="78"
           class="disabled-slider"
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
    <button class="btn-action btn-reboot"   id="btn-reboot" onclick="doReboot()">Reboot</button>
  </div>
</div>

<!-- curl -->
<div class="card curl-card">
  <div class="curl-label">API · curl examples</div>
  <div class="curl-row">
    <code class="curl-line" id="curl-time">curl http://&lt;IP&gt;/api/time</code>
    <button class="curl-copy" onclick="copyCmd('curl-time',this)">copy</button>
  </div>
  <div class="curl-row">
    <code class="curl-line" id="curl-stats">curl http://&lt;IP&gt;/api/stats</code>
    <button class="curl-copy" onclick="copyCmd('curl-stats',this)">copy</button>
  </div>
  <div class="curl-row">
    <code class="curl-line" id="curl-bright">curl -X POST http://&lt;IP&gt;/api/brightness -d "value=80"</code>
    <button class="curl-copy" onclick="copyCmd('curl-bright',this)">copy</button>
  </div>
  <div class="curl-row">
    <code class="curl-line" id="curl-power">curl -X POST http://&lt;IP&gt;/api/power -d "on=0"</code>
    <button class="curl-copy" onclick="copyCmd('curl-power',this)">copy</button>
  </div>
  <div class="curl-line curl-note"># brightness auto: -d "auto=1" &nbsp;|&nbsp; power on: -d "on=1"</div>
</div>

<!-- Chip info -->
<div class="card chip-card">
  <div class="chip-label">Microcontroller · ESP32-C3 Super Mini</div>
  <div class="chip-grid">
    <div class="chip-item">
      <span class="chip-key">Architecture</span>
      <span class="chip-val">RISC-V 32-bit</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">Core / Freq</span>
      <span class="chip-val">1× up to 160 MHz</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">Flash</span>
      <span class="chip-val">4 MB</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">SRAM</span>
      <span class="chip-val">400 KB</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">WiFi</span>
      <span class="chip-val">802.11 b/g/n 2.4 GHz</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">Bluetooth</span>
      <span class="chip-val">BLE 5.0</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">Display</span>
      <span class="chip-val">SSD1322 256×64 SPI</span>
    </div>
    <div class="chip-item">
      <span class="chip-key">Protocol</span>
      <span class="chip-val">REST + WebSocket :81</span>
    </div>
  </div>
</div>

<div class="status-bar">
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
    document.getElementById('theme-btn').textContent = t === 'dark' ? '🌙' : '☀️';
  }
  function toggleTheme() {
    const next = document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
    applyTheme(next);
    localStorage.setItem('theme', next);
  }

  // ── Состояние ────────────────────────────────────────
  let isManual          = false;
  let isDisplayOn       = true;
  let isDragging        = false;
  let modeChangePending = false;

  const slider = document.getElementById('brightness-slider');

  slider.addEventListener('mousedown',  () => { isDragging = true;  });
  slider.addEventListener('touchstart', () => { isDragging = true;  }, { passive: true });
  slider.addEventListener('mouseup',    () => { isDragging = false; });
  slider.addEventListener('touchend',   () => { isDragging = false; });

  // ── Слайдер ──────────────────────────────────────────
  function onSliderInput(val) {
    val = parseInt(val);
    slider.style.background =
      'linear-gradient(var(--accent), var(--accent)) 0/' + val + '% 100% no-repeat, var(--slider-bg)';
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
    const manual = (mode === 'manual');

    // Если кнопка уже активна — ничего не делаем
    if (manual === isManual) return;

    isManual = manual;
    modeChangePending = true;
    _applyModeUI(manual);

    const body = manual
      ? 'value=' + slider.value
      : 'auto=1';

    fetch('/api/brightness', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    })
    .then(() => {
      modeChangePending = false;
      if (!manual) showToast('Auto brightness enabled');
    })
    .catch(() => { modeChangePending = false; });
  }

  function _applyModeUI(manual) {
    const btnAuto   = document.getElementById('btn-auto');
    const btnManual = document.getElementById('btn-manual');

    // Убираем active с обеих, потом ставим нужной
    btnAuto.classList.remove('active');
    btnManual.classList.remove('active');

    if (manual) {
      btnManual.classList.add('active');
      slider.classList.remove('disabled-slider');
    } else {
      btnAuto.classList.add('active');
      slider.classList.add('disabled-slider');
    }
  }

  // ── Питание ──────────────────────────────────────────
  function togglePower() {
    const btn = document.getElementById('btn-power');
    btn.disabled = true;
    fetch('/api/power', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'on=' + (isDisplayOn ? '0' : '1')
    })
    .finally(() => { btn.disabled = false; });
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
    ws = new WebSocket('ws://' + location.hostname + ':81/');
    ws.onopen    = () => { setWsStatus(true);  clearTimeout(wsReconnectTimer); };
    ws.onmessage = (e) => { try { updateUI(JSON.parse(e.data)); } catch(_) {} };
    ws.onclose   = () => { setWsStatus(false); wsReconnectTimer = setTimeout(wsConnect, 3000); };
    ws.onerror   = () => ws.close();
  }
  function setWsStatus(ok) {
    document.getElementById('ws-dot').className    = 'ws-dot ' + (ok ? 'connected' : 'disconnected');
    document.getElementById('ws-label').textContent = ok ? 'LIVE' : 'OFFLINE';
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
    document.getElementById('curl-time')  .textContent = 'curl http://' + d.ip + '/api/time';
    document.getElementById('curl-stats') .textContent = 'curl http://' + d.ip + '/api/stats';
    document.getElementById('curl-bright').textContent = 'curl -X POST http://' + d.ip + '/api/brightness -d "value=80"';
    document.getElementById('curl-power') .textContent = 'curl -X POST http://' + d.ip + '/api/power -d "on=0"';

    // Слайдер — не трогаем пока пользователь его двигает или есть pending-запрос
    if (!isDragging && !modeChangePending && !isManual) {
      const pct = d.brightness_pct;
      slider.value = pct;
      slider.style.background =
        'linear-gradient(var(--accent), var(--accent)) 0/' + pct + '% 100% no-repeat, var(--slider-bg)';
      document.getElementById('brightness-pct').textContent = pct;
    }

    // Режим — синхронизируем только если нет pending-запроса
    if (!modeChangePending) {
      const serverManual = (d.brightness_manual === true);
      if (serverManual !== isManual) {
        isManual = serverManual;
        _applyModeUI(serverManual);
      }
    }

    // Питание
    if (d.display_on !== isDisplayOn) {
      _applyPowerUI(d.display_on);
    }

    // CPU
    const cpuEl = document.getElementById('cpu-bar');
    cpuEl.style.width = d.cpu + '%';
    cpuEl.className = 'cpu-mini-fill' + (d.cpu > 80 ? ' crit' : d.cpu > 50 ? ' warn' : '');

    // WiFi dots
    const lvl = d.rssi >= -50 ? 4 : d.rssi >= -60 ? 3 : d.rssi >= -70 ? 2 : 1;
    for (let i = 1; i <= 4; i++)
      document.getElementById('d' + i).className = i <= lvl ? 'on' : '';

    // RAM
    const used = d.ram_total - d.ram_free;
    const pct  = Math.round(used * 100 / d.ram_total);
    document.getElementById('ram-txt').textContent =
      Math.round(d.ram_free / 1024) + ' KB free  ·  ' +
      Math.round(d.ram_total / 1024) + ' KB total  ·  ' + pct + '% used';
    const ramBar = document.getElementById('ram-bar');
    ramBar.style.width = pct + '%';
    ramBar.className = 'ram-bar-fill' + (pct > 80 ? ' crit' : pct > 60 ? ' warn' : '');
  }

  // ── Reboot ────────────────────────────────────────────
  function showToast(msg, ms) {
    ms = ms || 2500;
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(function() { t.classList.remove('show'); }, ms);
  }

  function doReboot() {
    if (!confirm('Reboot the clock?')) return;
    const btn = document.getElementById('btn-reboot');
    btn.disabled = true;
    btn.textContent = 'Rebooting…';
    fetch('/api/reboot', { method: 'POST' }).catch(function(){});
    showToast('Rebooting… reconnecting in 6s', 6000);
    setTimeout(function() {
      btn.disabled = false;
      btn.textContent = 'Reboot';
    }, 7000);
  }

  wsConnect();

  // ── Copy curl ─────────────────────────────────────────
  function copyCmd(id, btn) {
    const text = document.getElementById(id).textContent.trim();

    function done() {
      btn.textContent = '✓ copied';
      btn.classList.add('copied');
      setTimeout(function() { btn.textContent = 'copy'; btn.classList.remove('copied'); }, 1800);
    }

    function fallback() {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.cssText = 'position:absolute;left:-9999px;top:-9999px;width:1px;height:1px';
      document.body.appendChild(ta);
      ta.select();
      ta.setSelectionRange(0, text.length);
      try { if (document.execCommand('copy')) done(); } catch(e) {}
      document.body.removeChild(ta);
    }

    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(done).catch(fallback);
    } else {
      fallback();
    }
  }
</script>
</body>
</html>
)rawliteral";
