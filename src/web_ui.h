#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Clock</title>sudo dmesg | grep tty
[sudo] password for andrew: 
[    0.177606] printk: legacy console [tty0] enabled
[    3.747882] cdc_acm 1-2:1.0: ttyACM0: USB ACM device
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@500;700&display=swap" rel="stylesheet">
<style>
  *, *::before, *::after { margin:0; padding:0; box-sizing:border-box; }

  :root {
    --bg:          #0a1e46;
    --bg-2:        #0d2352;
    --card:        #0c214c;
    --card-hi:     rgba(122,176,255,0.06);
    --border:      #1c3d76;
    --text:        #dbe8ff;
    --text-dim:    #6c9bda;
    --text-faint:  #33578f;
    --accent:      #5b9bff;
    --accent-soft: rgba(91,155,255,0.16);
    --curl-bg:     #050c1c;
    --curl-border: #122542;
    --btn-bg:      #16295380;
    --btn-color:   #8dbaff;
    --wifi-off:    #1c3d76;
    --slider-bg:   #071223;
    --shadow:      0 10px 30px rgba(0,0,0,0.35);
  }

  [data-theme="light"] {
    --bg:          #e5eeff;
    --bg-2:        #d7e5ff;
    --card:        #f4f8ff;
    --card-hi:     rgba(255,255,255,0.7);
    --border:      #b2ccf0;
    --text:        #0a1f47;
    --text-dim:    #2a5db0;
    --text-faint:  #6b95d4;
    --accent:      #1f66d6;
    --accent-soft: rgba(31,102,214,0.12);
    --curl-bg:     #e6efff;
    --curl-border: #b2ccf0;
    --btn-bg:      #dbe9ff;
    --btn-color:   #1a3f92;
    --wifi-off:    #bcd2f2;
    --slider-bg:   #cdddf6;
    --shadow:      0 8px 24px rgba(31,66,132,0.14);
  }

  body {
    background:
      radial-gradient(1200px 600px at 50% -10%, var(--bg-2), transparent 60%),
      var(--bg);
    color: var(--text);
    font-family: 'Inter', system-ui, sans-serif;
    min-height: 100vh; display: flex; flex-direction: column;
    align-items: center; justify-content: flex-start;
    padding: 22px 16px 40px; gap: 12px;
    transition: background 0.3s, color 0.3s;
  }

  .card {
    background:
      linear-gradient(var(--card-hi), transparent 42%),
      var(--card);
    border: 1px solid var(--border);
    border-radius: 18px; width: min(540px, 100%);
    box-shadow: var(--shadow);
    transition: background 0.3s, border-color 0.3s;
  }

  /* ── Шапка ── */
  .app-header {
    width: min(540px, 100%);
    display: flex; align-items: center; justify-content: space-between;
    padding: 2px 4px 4px;
  }
  .app-title {
    font-size: 13px; font-weight: 700; letter-spacing: 4px;
    text-transform: uppercase; color: var(--text-dim);
  }
  .app-title b { color: var(--accent); font-weight: 700; }
  .host-chip {
    font-family: 'JetBrains Mono', monospace; font-size: 11px;
    color: var(--text-dim); background: var(--accent-soft);
    border: 1px solid var(--border); border-radius: 999px;
    padding: 4px 11px; letter-spacing: 0.5px;
  }

  .theme-btn {
    position: fixed; top: 14px; right: 14px;
    width: 40px; height: 40px; border-radius: 50%;
    background: var(--card); border: 1px solid var(--border);
    color: var(--text-dim); font-size: 18px; cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    z-index: 50; box-shadow: var(--shadow);
    transition: background 0.2s, border-color 0.2s, transform 0.15s;
  }
  .theme-btn:hover { border-color: var(--accent); color: var(--accent); transform: rotate(-12deg); }

  /* ── Часы ── */
  .clock-card { padding: 30px 24px 22px; text-align: center; overflow: hidden; }
  #time {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(44px, 10vw, 82px); font-weight: 700;
    letter-spacing: -1px; line-height: 1; color: var(--text); white-space: nowrap;
  }
  /* Сигнатурный элемент: полоса прогресса секунд */
  .sec-track {
    height: 3px; border-radius: 3px; background: var(--slider-bg);
    margin: 20px auto 0; overflow: hidden; max-width: 320px;
  }
  #sec-bar {
    height: 100%; width: 0%; border-radius: 3px;
    background: linear-gradient(90deg, var(--accent), #9ec5ff);
    box-shadow: 0 0 8px var(--accent-soft);
    transition: width 0.9s linear;
  }
  .divider { border: none; border-top: 1px solid var(--border); margin: 16px 0 14px; }
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
  .wifi-dots span { display: inline-block; width: 6px; border-radius: 2px; background: var(--wifi-off); transition: background 0.3s; }
  .wifi-dots span.on { background: var(--accent); }

  .cpu-row       { display: flex; align-items: center; gap: 10px; }
  .cpu-mini-bar  { flex: 1; height: 5px; background: var(--slider-bg); border-radius: 3px; overflow: hidden; max-width: 80px; }
  .cpu-mini-fill { height: 100%; border-radius: 3px; background: var(--accent); transition: width 1s ease; }
  .cpu-mini-fill.warn { background: #d97706; }
  .cpu-mini-fill.crit { background: #dc2626; }

  /* ── Яркость ── */
  .brightness-card { padding: 20px 24px; }
  .brightness-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; }
  .brightness-left { display: flex; align-items: center; gap: 10px; }
  .brightness-title  { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  .level-chip {
    font-size: 10px; font-weight: 700; letter-spacing: 1px; text-transform: uppercase;
    color: var(--accent); background: var(--accent-soft);
    border-radius: 999px; padding: 3px 9px;
  }
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
    background: linear-gradient(var(--accent), var(--accent)) 0/50% 100% no-repeat,
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
    padding: 12px 24px; border-radius: 10px; box-shadow: var(--shadow);
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

  /* ── Секундомер ── */
  .stopwatch-card { padding: 22px 24px; }
  .stopwatch-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
  .stopwatch-title { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  .sw-sync { font-size: 10px; letter-spacing: 1px; text-transform: uppercase; color: var(--text-faint); }
  .sw-display {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(32px, 7vw, 56px); font-weight: 700;
    letter-spacing: 2px; text-align: center; color: var(--text);
    line-height: 1; margin-bottom: 16px;
  }
  .sw-display .sw-ms { font-size: 0.45em; color: var(--text-dim); vertical-align: baseline; }
  .sw-controls { display: flex; gap: 10px; justify-content: center; flex-wrap: wrap; }
  .sw-btn {
    font-family: 'Inter', sans-serif; font-size: 12px; font-weight: 700;
    letter-spacing: 1px; padding: 10px 22px; border-radius: 9px;
    cursor: pointer; text-transform: uppercase; white-space: nowrap;
    border: 1px solid var(--border); background: var(--btn-bg); color: var(--btn-color);
    transition: background 0.2s, border-color 0.2s, color 0.2s;
    min-width: 90px;
  }
  .sw-btn:disabled { opacity: 0.4; cursor: not-allowed; }
  #sw-start-btn { border-color: #16a34a; color: #16a34a; }
  #sw-start-btn:hover { background: #16a34a; color: #fff; }
  #sw-start-btn.running { border-color: #d97706; color: #d97706; }
  #sw-start-btn.running:hover { background: #d97706; color: #fff; }
  #sw-reset-btn:hover { border-color: var(--accent); color: var(--accent); }
  #sw-lap-btn:hover:not(:disabled) { border-color: var(--accent); color: var(--accent); }
  .sw-laps { margin-top: 14px; max-height: 160px; overflow-y: auto; display: flex; flex-direction: column; gap: 4px; }
  .sw-lap-item {
    display: flex; align-items: center; justify-content: space-between;
    font-family: 'JetBrains Mono', monospace; font-size: 13px;
    padding: 5px 10px; border-radius: 6px;
    background: var(--curl-bg); border: 1px solid var(--curl-border);
  }
  .sw-lap-num { color: var(--text-faint); font-size: 11px; }
  .sw-lap-time { color: var(--text); }
  .sw-lap-delta { color: var(--text-dim); font-size: 11px; }
  .sw-laps:empty { display: none; }

  /* ── Chip info card ── */
  .chip-card { padding: 18px 24px; }
  .chip-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; margin-bottom: 14px; }
  .chip-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px 20px; }
  .chip-item { display: flex; flex-direction: column; gap: 2px; }
  .chip-key  { font-size: 10px; font-weight: 700; color: var(--text-faint); letter-spacing: 1px; text-transform: uppercase; }
  .chip-val  { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 500; color: var(--text); }

  @media (prefers-reduced-motion: reduce) {
    * { animation: none !important; transition: none !important; }
  }
</style>
</head>
<body>

<button class="theme-btn" id="theme-btn" onclick="toggleTheme()" title="Toggle theme">🌙</button>

<!-- Шапка -->
<div class="app-header">
  <div class="app-title">ESP32 <b>CLOCK</b></div>
  <div class="host-chip" id="host-chip">clock.local</div>
</div>

<!-- Часы -->
<div class="card clock-card">
  <div id="time">--:--:--</div>
  <div class="sec-track"><div id="sec-bar"></div></div>
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
    <div class="brightness-left">
      <div class="brightness-title">Display Brightness</div>
      <span class="level-chip" id="bright-label">—</span>
    </div>
    <div class="brightness-mode">
      <button class="mode-btn active" id="btn-auto"   onclick="setBrightnessMode('auto')">Auto</button>
      <button class="mode-btn"        id="btn-manual" onclick="setBrightnessMode('manual')">Manual</button>
    </div>
  </div>
  <div class="slider-row">
    <span class="brightness-icon">🌑</span>
    <input type="range" id="brightness-slider"
           min="0" max="100" value="50"
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

<!-- Секундомер -->
<div class="card stopwatch-card">
  <div class="stopwatch-header">
    <div class="stopwatch-title">Stopwatch</div>
    <span class="sw-sync" id="sw-sync">synced with device</span>
  </div>
  <div class="sw-display" id="sw-display">00:00<span class="sw-ms">.000</span></div>
  <div class="sw-controls">
    <button class="sw-btn" id="sw-start-btn" onclick="swToggle()">Start</button>
    <button class="sw-btn" id="sw-lap-btn"   onclick="swLap()"    disabled>Lap</button>
    <button class="sw-btn" id="sw-reset-btn" onclick="swReset()">Reset</button>
  </div>
  <div class="sw-laps" id="sw-laps"></div>
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

  // Показываем реальный хост в чипе (clock.local или IP)
  document.getElementById('host-chip').textContent = location.hostname || 'clock.local';

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
  function paintSlider(val) {
    slider.style.background =
      'linear-gradient(var(--accent), var(--accent)) 0/' + val + '% 100% no-repeat, var(--slider-bg)';
  }
  function onSliderInput(val) {
    val = parseInt(val);
    paintSlider(val);
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
    if (manual === isManual) return;

    isManual = manual;
    modeChangePending = true;
    _applyModeUI(manual);

    const body = manual ? 'value=' + slider.value : 'auto=1';
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
    swServerSynced = false;              // при новом коннекте заново берём состояние секундомера с устройства
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

  // ── Полоса секунд ─────────────────────────────────────
  let lastSecPct = 0;
  function updateSecBar(timeStr) {
    const parts = (timeStr || '').split(':');
    if (parts.length !== 3) return;
    const sec = parseInt(parts[2], 10);
    if (isNaN(sec)) return;
    const pct = (sec / 60) * 100;
    const bar = document.getElementById('sec-bar');
    // при переходе на новую минуту не анимируем «назад»
    if (pct < lastSecPct) { bar.style.transition = 'none'; bar.style.width = pct + '%';
                            void bar.offsetWidth; bar.style.transition = ''; }
    else                  { bar.style.width = pct + '%'; }
    lastSecPct = pct;
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
    document.getElementById('req-count').textContent    = Number(d.requests).toLocaleString();
    document.getElementById('bright-label').textContent = d.brightness_label;

    updateSecBar(d.time);

    // curl-примеры под реальный IP
    document.getElementById('curl-time')  .textContent = 'curl http://' + d.ip + '/api/time';
    document.getElementById('curl-stats') .textContent = 'curl http://' + d.ip + '/api/stats';
    document.getElementById('curl-bright').textContent = 'curl -X POST http://' + d.ip + '/api/brightness -d "value=80"';
    document.getElementById('curl-power') .textContent = 'curl -X POST http://' + d.ip + '/api/power -d "on=0"';

    // Слайдер — не трогаем, пока пользователь двигает или есть pending-запрос
    if (!isDragging && !modeChangePending && !isManual) {
      const pct = d.brightness_pct;
      slider.value = pct;
      paintSlider(pct);
      document.getElementById('brightness-pct').textContent = pct;
    }

    // Режим яркости
    if (!modeChangePending) {
      const serverManual = (d.brightness_manual === true);
      if (serverManual !== isManual) {
        isManual = serverManual;
        _applyModeUI(serverManual);
      }
    }

    // Питание
    if (d.display_on !== isDisplayOn) _applyPowerUI(d.display_on);

    // CPU
    const cpuEl = document.getElementById('cpu-bar');
    cpuEl.style.width = d.cpu + '%';
    cpuEl.className = 'cpu-mini-fill' + (d.cpu > 80 ? ' crit' : d.cpu > 50 ? ' warn' : '');

    // WiFi
    const lvl = d.rssi >= -50 ? 4 : d.rssi >= -60 ? 3 : d.rssi >= -70 ? 2 : 1;
    for (let i = 1; i <= 4; i++)
      document.getElementById('d' + i).className = i <= lvl ? 'on' : '';

    // RAM
    const used = d.ram_total - d.ram_free;
    const rpct = Math.round(used * 100 / d.ram_total);
    document.getElementById('ram-txt').textContent =
      Math.round(d.ram_free / 1024) + ' KB free  ·  ' +
      Math.round(d.ram_total / 1024) + ' KB total  ·  ' + rpct + '% used';
    const ramBar = document.getElementById('ram-bar');
    ramBar.style.width = rpct + '%';
    ramBar.className = 'ram-bar-fill' + (rpct > 80 ? ' crit' : rpct > 60 ? ' warn' : '');

    // Секундомер — берём состояние с устройства при первом сообщении/реконнекте
    if (!swServerSynced && d.sw_state !== undefined) {
      adoptStopwatch(d.sw_state, d.sw_ms);
      swServerSynced = true;
    }
  }

  // ── Toast / Reboot ────────────────────────────────────
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

  // ── Секундомер ───────────────────────────────────────
  let swRunning     = false;
  let swStartTs     = 0;
  let swAccum       = 0;
  let swRafId       = null;
  let swLaps        = [];
  let swLastLapMs   = 0;
  let swServerSynced = false;

  function swSend(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(cmd);
  }

  // Приводим локальный секундомер к состоянию устройства (state: 0=idle,1=run,2=pause)
  function adoptStopwatch(state, ms) {
    cancelAnimationFrame(swRafId);
    swLaps = []; swLastLapMs = 0;
    document.getElementById('sw-laps').innerHTML = '';
    const startBtn = document.getElementById('sw-start-btn');
    const lapBtn   = document.getElementById('sw-lap-btn');

    if (state === 1) {                 // RUNNING
      swRunning = true;
      swAccum   = ms;
      swStartTs = performance.now();
      startBtn.textContent = 'Pause';
      startBtn.classList.add('running');
      lapBtn.disabled = false;
      swTick();
    } else if (state === 2) {          // PAUSED
      swRunning = false;
      swAccum   = ms;
      swStartTs = 0;
      swRender(ms);
      startBtn.textContent = 'Resume';
      startBtn.classList.remove('running');
      lapBtn.disabled = (ms <= 0);
    } else {                           // IDLE
      swRunning = false;
      swAccum   = 0;
      swStartTs = 0;
      swRender(0);
      startBtn.textContent = 'Start';
      startBtn.classList.remove('running');
      lapBtn.disabled = true;
    }
  }

  function swToggle() {
    const startBtn = document.getElementById('sw-start-btn');
    if (!swRunning) {
      swRunning = true;
      swStartTs = performance.now();
      swSend('sw:start');
      startBtn.textContent = 'Pause';
      startBtn.classList.add('running');
      document.getElementById('sw-lap-btn').disabled = false;
      swTick();
    } else {
      swRunning = false;
      swAccum  += performance.now() - swStartTs;
      cancelAnimationFrame(swRafId);
      swSend('sw:pause');
      startBtn.textContent = 'Resume';
      startBtn.classList.remove('running');
    }
  }

  function swTick() {
    const now = swAccum + (performance.now() - swStartTs);
    swRender(now);
    if (swRunning) swRafId = requestAnimationFrame(swTick);
  }

  function swRender(ms) {
    const totalMs = Math.floor(ms);
    const mins    = Math.floor(totalMs / 60000);
    const secs    = Math.floor((totalMs % 60000) / 1000);
    const millis  = totalMs % 1000;
    document.getElementById('sw-display').innerHTML =
      String(mins).padStart(2,'0') + ':' + String(secs).padStart(2,'0') +
      '<span class="sw-ms">.' + String(millis).padStart(3,'0') + '</span>';
  }

  function swLap() {
    const now   = swAccum + (swRunning ? (performance.now() - swStartTs) : 0);
    const delta = now - swLastLapMs;
    swLastLapMs = now;
    swLaps.push({ total: now, delta: delta });
    const container = document.getElementById('sw-laps');
    const row = document.createElement('div');
    row.className = 'sw-lap-item';
    row.innerHTML =
      '<span class="sw-lap-num">Lap ' + swLaps.length + '</span>' +
      '<span class="sw-lap-time">' + swFmt(now) + '</span>' +
      '<span class="sw-lap-delta">+' + swFmt(delta) + '</span>';
    container.prepend(row);
  }

  function swReset() {
    swRunning   = false;
    swAccum     = 0;
    swStartTs   = 0;
    swLastLapMs = 0;
    swLaps      = [];
    cancelAnimationFrame(swRafId);
    swRender(0);
    swSend('sw:reset');
    document.getElementById('sw-start-btn').textContent = 'Start';
    document.getElementById('sw-start-btn').classList.remove('running');
    document.getElementById('sw-lap-btn').disabled = true;
    document.getElementById('sw-laps').innerHTML = '';
  }

  function swFmt(ms) {
    ms = Math.floor(ms);
    const mi = Math.floor(ms / 60000);
    const sc = Math.floor((ms % 60000) / 1000);
    const ml = ms % 1000;
    return String(mi).padStart(2,'0') + ':' + String(sc).padStart(2,'0') + '.' + String(ml).padStart(3,'0');
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
