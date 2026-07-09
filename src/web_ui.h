#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Clock</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap" rel="stylesheet">
<style>
  *, *::before, *::after { margin:0; padding:0; box-sizing:border-box; }

  :root {
    --bg:          #0e0c09;
    --bg-2:        #171009;
    --surface:     #17130d;
    --surface-2:   #1d170e;
    --line:        #2c2318;
    --line-soft:   #241d13;
    --text:        #f3ead9;
    --text-dim:    #b39c7d;
    --text-faint:  #766444;
    --accent:      #f5b13c;
    --accent-2:    #ffd486;
    --accent-soft: rgba(245,177,60,0.14);
    --good:        #64d19b;
    --warn:        #f0a53c;
    --crit:        #ef6a5a;
    --slider-bg:   #241d13;
    --code:        #ffcf7a;
    --shadow:      0 18px 46px rgba(0,0,0,0.55);
    --glow:        0 0 34px rgba(245,177,60,0.16);
  }

  [data-theme="light"] {
    --bg:          #f6f0e5;
    --bg-2:        #efe6d4;
    --surface:     #fffdf8;
    --surface-2:   #fbf5ea;
    --line:        #e7dcc6;
    --line-soft:   #efe6d5;
    --text:        #2b2114;
    --text-dim:    #7c6a4d;
    --text-faint:  #ab9a7c;
    --accent:      #c07d12;
    --accent-2:    #a9690a;
    --accent-soft: rgba(192,125,18,0.13);
    --good:        #2f9e6a;
    --warn:        #c07d12;
    --crit:        #cf4b3a;
    --slider-bg:   #eaddc5;
    --code:        #8a5a08;
    --shadow:      0 14px 34px rgba(120,90,40,0.16);
    --glow:        0 0 26px rgba(192,125,18,0.14);
  }

  body {
    background:
      radial-gradient(1100px 520px at 50% -8%, var(--bg-2), transparent 62%),
      var(--bg);
    color: var(--text);
    font-family: 'Space Grotesk', system-ui, sans-serif;
    min-height: 100vh; display: flex; flex-direction: column;
    align-items: center; justify-content: flex-start;
    padding: 24px 16px 48px; gap: 13px;
    -webkit-font-smoothing: antialiased;
    transition: background 0.4s, color 0.4s;
  }

  .card {
    position: relative;
    background: var(--surface);
    border: 1px solid var(--line);
    border-radius: 20px; width: min(560px, 100%);
    box-shadow: var(--shadow);
    animation: rise 0.5s cubic-bezier(.2,.7,.3,1) both;
    transition: border-color 0.25s, transform 0.25s, box-shadow 0.25s;
  }
  .card:hover { border-color: var(--line); transform: translateY(-1px); }
  @keyframes rise { from { opacity:0; transform: translateY(10px); } to { opacity:1; transform: none; } }
  .card:nth-of-type(2){animation-delay:.04s}.card:nth-of-type(3){animation-delay:.08s}
  .card:nth-of-type(4){animation-delay:.12s}.card:nth-of-type(5){animation-delay:.16s}
  .card:nth-of-type(6){animation-delay:.20s}.card:nth-of-type(7){animation-delay:.24s}

  .eyebrow { font-size: 10px; font-weight: 600; color: var(--text-faint);
             letter-spacing: 3px; text-transform: uppercase; }

  /* ── Шапка ── */
  .app-header {
    width: min(560px, 100%);
    display: flex; align-items: center; justify-content: space-between;
    padding: 4px 6px 2px;
  }
  .app-title { font-size: 13px; font-weight: 700; letter-spacing: 5px;
               text-transform: uppercase; color: var(--text-dim); }
  .app-title b { color: var(--accent); }
  .host-chip {
    font-family: 'JetBrains Mono', monospace; font-size: 11px;
    color: var(--accent); background: var(--accent-soft);
    border: 1px solid var(--line); border-radius: 999px;
    padding: 4px 12px; letter-spacing: 0.4px;
  }

  .theme-btn {
    position: fixed; top: 16px; right: 16px;
    width: 42px; height: 42px; border-radius: 50%;
    background: var(--surface); border: 1px solid var(--line);
    color: var(--accent); font-size: 18px; cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    z-index: 50; box-shadow: var(--shadow);
    transition: transform 0.25s, border-color 0.2s;
  }
  .theme-btn:hover { border-color: var(--accent); transform: rotate(-18deg) scale(1.05); }

  /* ── Часы ── */
  .clock-card { padding: 34px 26px 26px; text-align: center; overflow: hidden; }
  .clock-card::before {
    content: ''; position: absolute; inset: 0;
    background: radial-gradient(420px 150px at 50% 22%, var(--accent-soft), transparent 70%);
    pointer-events: none;
  }
  .clock-eyebrow { position: relative; margin-bottom: 12px; }
  #time {
    position: relative;
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(46px, 11vw, 88px); font-weight: 700;
    letter-spacing: -1px; line-height: 0.98; color: var(--text);
    white-space: nowrap;
    text-shadow: 0 0 22px rgba(245,177,60,0.28);
  }
  [data-theme="light"] #time { text-shadow: none; }
  .sec-track { position: relative; height: 3px; border-radius: 3px;
               background: var(--slider-bg); margin: 22px auto 0;
               overflow: hidden; max-width: 340px; }
  #sec-bar { height: 100%; width: 0%; border-radius: 3px;
             background: linear-gradient(90deg, var(--accent), var(--accent-2));
             box-shadow: 0 0 10px var(--accent-soft);
             transition: width 0.9s linear; }
  .clock-meta { position: relative; display: flex; align-items: center;
                justify-content: center; gap: 12px; margin-top: 20px; flex-wrap: wrap; }
  #date { font-family: 'JetBrains Mono', monospace; font-size: clamp(14px,3.4vw,19px);
          font-weight: 500; color: var(--text-dim); letter-spacing: 2px; text-transform: uppercase; }
  .meta-dot { width: 4px; height: 4px; border-radius: 50%; background: var(--accent); opacity: .7; }
  #day { font-size: clamp(11px,2.4vw,13px); font-weight: 600; color: var(--text-faint);
         letter-spacing: 4px; text-transform: uppercase; }

  /* ── Секции ── */
  .section-head { display: flex; align-items: center; justify-content: space-between; margin-bottom: 16px; }
  .section-title { font-size: 11px; font-weight: 700; color: var(--text-faint);
                   letter-spacing: 2px; text-transform: uppercase; }

  /* ── Статистика ── */
  .stats-card { padding: 22px 24px; }
  .stat-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
  .tile { background: var(--surface-2); border: 1px solid var(--line-soft);
          border-radius: 13px; padding: 13px 15px; display: flex;
          flex-direction: column; gap: 7px; min-width: 0; }
  .tile.wide { grid-column: 1 / -1; }
  .tile-label { font-size: 10px; font-weight: 700; color: var(--text-faint);
                letter-spacing: 1.5px; text-transform: uppercase; }
  .tile-value { font-family: 'JetBrains Mono', monospace; font-size: 18px; font-weight: 500;
                color: var(--text); line-height: 1.2; white-space: nowrap;
                overflow: hidden; text-overflow: ellipsis; }
  .unit { font-size: 12px; color: var(--text-dim); margin-left: 3px; }

  .req-badge { display: inline-flex; align-items: center; gap: 7px; }
  .req-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--accent);
             flex-shrink: 0; animation: pulse-dot 2s infinite; box-shadow: 0 0 8px var(--accent); }
  @keyframes pulse-dot { 0%,100%{opacity:1;transform:scale(1)} 50%{opacity:.35;transform:scale(.7)} }

  .inline-row { display: flex; align-items: center; gap: 10px; }
  .wifi-dots { display: inline-flex; gap: 3px; align-items: flex-end; margin-left: auto; }
  .wifi-dots span { display: inline-block; width: 6px; border-radius: 2px; background: var(--slider-bg); transition: background 0.3s; }
  .wifi-dots span.on { background: var(--accent); box-shadow: 0 0 6px var(--accent-soft); }

  .mini-bar { flex: 1; height: 5px; background: var(--slider-bg); border-radius: 3px; overflow: hidden; min-width: 40px; }
  .cpu-mini-fill, .ram-bar-fill { height: 100%; border-radius: 3px; background: var(--accent); transition: width 1s ease; }
  .cpu-mini-fill.warn, .ram-bar-fill.warn { background: var(--warn); }
  .cpu-mini-fill.crit, .ram-bar-fill.crit { background: var(--crit); }
  .ram-sub { font-family: 'JetBrains Mono', monospace; font-size: 12px; color: var(--text-dim); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

  /* ── Яркость ── */
  .brightness-card { padding: 22px 24px; }
  .brightness-left { display: flex; align-items: center; gap: 10px; }
  .level-chip { font-size: 10px; font-weight: 700; letter-spacing: 1px; text-transform: uppercase;
                color: var(--accent); background: var(--accent-soft); border-radius: 999px; padding: 3px 10px; }
  .seg { display: flex; gap: 4px; background: var(--surface-2); border: 1px solid var(--line-soft);
         border-radius: 9px; padding: 3px; }
  .mode-btn { font-size: 11px; font-weight: 600; letter-spacing: 1px; padding: 5px 14px;
              border-radius: 6px; border: none; background: transparent; color: var(--text-faint);
              text-transform: uppercase; cursor: pointer; user-select: none; -webkit-user-select: none;
              transition: background 0.15s, color 0.15s; }
  .mode-btn.active { background: var(--accent); color: #1a1206; pointer-events: none; }
  .mode-btn:not(.active):hover { color: var(--text-dim); }

  .slider-row { display: flex; align-items: center; gap: 14px; }
  .brightness-icon { font-size: 14px; flex-shrink: 0; opacity: 0.65; }
  input[type="range"] {
    -webkit-appearance: none; appearance: none; flex: 1; height: 6px; border-radius: 3px; outline: none;
    background: linear-gradient(var(--accent), var(--accent)) 0/50% 100% no-repeat, var(--slider-bg);
    cursor: pointer; transition: opacity 0.2s;
  }
  input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none; appearance: none; width: 18px; height: 18px; border-radius: 50%;
    background: var(--accent-2); border: 2px solid var(--accent);
    box-shadow: 0 0 10px var(--accent-soft); cursor: pointer; transition: transform 0.15s;
  }
  input[type="range"]::-webkit-slider-thumb:hover { transform: scale(1.18); }
  input[type="range"]::-moz-range-thumb { width: 18px; height: 18px; border-radius: 50%;
    background: var(--accent-2); border: 2px solid var(--accent); cursor: pointer; }
  input[type="range"].disabled-slider { opacity: 0.3; pointer-events: none; cursor: not-allowed; }
  .brightness-val { font-family: 'JetBrains Mono', monospace; font-size: 15px; font-weight: 600;
                    color: var(--text); min-width: 42px; text-align: right; }

  /* ── Управление ── */
  .controls-card { padding: 18px 24px; display: flex; align-items: center;
                   justify-content: space-between; gap: 14px; flex-wrap: wrap; }
  .controls-left { display: flex; flex-direction: column; gap: 4px; }
  .controls-label { font-size: 10px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  #uptime-label { font-family: 'JetBrains Mono', monospace; font-size: 18px; color: var(--text); }
  .controls-right { display: flex; gap: 10px; flex-shrink: 0; }
  .btn-action { font-family: 'Space Grotesk', sans-serif; font-size: 12px; font-weight: 700;
                letter-spacing: 0.6px; padding: 11px 18px; border-radius: 11px; cursor: pointer;
                text-transform: uppercase; white-space: nowrap; border: 1px solid var(--line);
                background: var(--surface-2); color: var(--text-dim);
                transition: background 0.2s, border-color 0.2s, color 0.2s; }
  .btn-action:disabled { opacity: 0.4; cursor: not-allowed; }
  .btn-power.on { border-color: var(--good); color: var(--good); }
  .btn-power.off { border-color: var(--crit); color: var(--crit); }
  .btn-power.on:hover:not(:disabled) { background: var(--good); color: #08130c; }
  .btn-power.off:hover:not(:disabled) { background: var(--crit); color: #1a0805; }
  .btn-reboot:hover:not(:disabled) { background: var(--crit); border-color: var(--crit); color: #1a0805; }

  /* ── Секундомер ── */
  .stopwatch-card { padding: 22px 24px; }
  .sw-sync { font-size: 10px; letter-spacing: 1px; text-transform: uppercase; color: var(--text-faint); }
  .sw-display { font-family: 'JetBrains Mono', monospace; font-size: clamp(34px,8vw,60px);
                font-weight: 700; letter-spacing: 2px; text-align: center; color: var(--text);
                line-height: 1; margin: 6px 0 18px; text-shadow: 0 0 18px rgba(245,177,60,0.18); }
  [data-theme="light"] .sw-display { text-shadow: none; }
  .sw-display .sw-ms { font-size: 0.42em; color: var(--accent); vertical-align: baseline; }
  .sw-controls { display: flex; gap: 10px; justify-content: center; flex-wrap: wrap; }
  .sw-btn { font-family: 'Space Grotesk', sans-serif; font-size: 12px; font-weight: 700;
            letter-spacing: 0.6px; padding: 11px 22px; border-radius: 11px; cursor: pointer;
            text-transform: uppercase; white-space: nowrap; border: 1px solid var(--line);
            background: var(--surface-2); color: var(--text-dim); min-width: 92px;
            transition: background 0.2s, border-color 0.2s, color 0.2s; }
  .sw-btn:disabled { opacity: 0.4; cursor: not-allowed; }
  #sw-start-btn { border-color: var(--good); color: var(--good); }
  #sw-start-btn:hover { background: var(--good); color: #08130c; }
  #sw-start-btn.running { border-color: var(--warn); color: var(--warn); }
  #sw-start-btn.running:hover { background: var(--warn); color: #1a1206; }
  #sw-reset-btn:hover:not(:disabled), #sw-lap-btn:hover:not(:disabled) { border-color: var(--accent); color: var(--accent); }
  .sw-laps { margin-top: 16px; max-height: 168px; overflow-y: auto; display: flex; flex-direction: column; gap: 5px; }
  .sw-lap-item { display: flex; align-items: center; justify-content: space-between;
                 font-family: 'JetBrains Mono', monospace; font-size: 13px; padding: 6px 12px;
                 border-radius: 8px; background: var(--surface-2); border: 1px solid var(--line-soft); }
  .sw-lap-num { color: var(--text-faint); font-size: 11px; }
  .sw-lap-time { color: var(--text); }
  .sw-lap-delta { color: var(--accent); font-size: 11px; }
  .sw-laps:empty { display: none; }

  /* ── curl ── */
  .curl-card { padding: 20px 24px; }
  .curl-row { display: flex; align-items: center; gap: 10px; margin-bottom: 8px; }
  .curl-line { font-family: 'JetBrains Mono', monospace; font-size: 12.5px; color: var(--code);
               flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; user-select: all;
               background: var(--surface-2); border: 1px solid var(--line-soft);
               border-radius: 8px; padding: 8px 11px; }
  .curl-copy { flex-shrink: 0; padding: 6px 11px; border-radius: 7px; font-size: 10px; font-weight: 700;
               letter-spacing: 0.6px; border: 1px solid var(--line); background: var(--surface-2);
               color: var(--text-faint); cursor: pointer; text-transform: uppercase;
               font-family: 'Space Grotesk', sans-serif;
               transition: border-color 0.15s, color 0.15s; }
  .curl-copy:hover { border-color: var(--accent); color: var(--accent); }
  .curl-copy.copied { border-color: var(--good); color: var(--good); }
  .curl-note { font-family: 'JetBrains Mono', monospace; color: var(--text-faint); font-size: 11px; margin-top: 6px; }

  /* ── Chip info ── */
  .chip-card { padding: 20px 24px; }
  .chip-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px 20px; }
  .chip-item { display: flex; flex-direction: column; gap: 3px; }
  .chip-key { font-size: 10px; font-weight: 700; color: var(--text-faint); letter-spacing: 1px; text-transform: uppercase; }
  .chip-val { font-family: 'JetBrains Mono', monospace; font-size: 13px; font-weight: 500; color: var(--text); }

  /* ── Статус / toast ── */
  .status-bar { display: flex; align-items: center; gap: 14px; font-size: 11px; font-weight: 500;
                color: var(--text-faint); letter-spacing: 1.5px; margin-top: 4px; }
  .ws-indicator { display: flex; align-items: center; gap: 6px; font-size: 10px; letter-spacing: 1px; text-transform: uppercase; }
  .ws-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--text-faint); transition: background 0.3s; }
  .ws-dot.connected { background: var(--good); animation: pulse 2s infinite; box-shadow: 0 0 8px var(--good); }
  .ws-dot.disconnected { background: var(--crit); }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.25} }
  .toast { position: fixed; bottom: 26px; left: 50%; transform: translateX(-50%);
           background: var(--surface); border: 1px solid var(--accent); color: var(--text);
           font-size: 14px; font-weight: 500; padding: 13px 24px; border-radius: 12px;
           box-shadow: var(--shadow); opacity: 0; transition: opacity 0.3s;
           pointer-events: none; white-space: nowrap; z-index: 100; }
  .toast.show { opacity: 1; }

  @media (max-width: 420px) {
    .stat-grid { grid-template-columns: 1fr; }
    .chip-grid { grid-template-columns: 1fr; }
  }
  @media (prefers-reduced-motion: reduce) {
    *, .card { animation: none !important; transition: none !important; }
  }
</style>
</head>
<body>

<button class="theme-btn" id="theme-btn" onclick="toggleTheme()" title="Toggle theme">🌙</button>

<div class="app-header">
  <div class="app-title">ESP32 <b>CLOCK</b></div>
  <div class="host-chip" id="host-chip">clock.local</div>
</div>

<!-- Часы -->
<div class="card clock-card">
  <div class="eyebrow clock-eyebrow">Local time</div>
  <div id="time">--:--:--</div>
  <div class="sec-track"><div id="sec-bar"></div></div>
  <div class="clock-meta">
    <span id="date">-- --- ----</span>
    <span class="meta-dot"></span>
    <span id="day">---------</span>
  </div>
</div>

<!-- Секундомер -->
<div class="card stopwatch-card">
  <div class="section-head">
    <div class="section-title">Stopwatch</div>
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

<!-- Яркость -->
<div class="card brightness-card">
  <div class="section-head">
    <div class="brightness-left">
      <div class="section-title">Brightness</div>
      <span class="level-chip" id="bright-label">—</span>
    </div>
    <div class="seg">
      <button class="mode-btn active" id="btn-auto"   onclick="setBrightnessMode('auto')">Auto</button>
      <button class="mode-btn"        id="btn-manual" onclick="setBrightnessMode('manual')">Manual</button>
    </div>
  </div>
  <div class="slider-row">
    <span class="brightness-icon">🌑</span>
    <input type="range" id="brightness-slider" min="0" max="100" value="50"
           class="disabled-slider"
           oninput="onSliderInput(this.value)" onchange="sendBrightness(this.value)">
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

<!-- Статистика -->
<div class="card stats-card">
  <div class="section-head"><div class="section-title">Device stats</div></div>
  <div class="stat-grid">
    <div class="tile">
      <div class="tile-label">Die temp</div>
      <div class="tile-value"><span id="temp">—</span><span class="unit">°C</span></div>
    </div>
    <div class="tile">
      <div class="tile-label">CPU load</div>
      <div class="inline-row">
        <div class="tile-value"><span id="cpu-val">—</span><span class="unit">%</span></div>
        <div class="mini-bar"><div class="cpu-mini-fill" id="cpu-bar" style="width:0%"></div></div>
      </div>
    </div>
    <div class="tile">
      <div class="tile-label">WiFi SSID</div>
      <div class="tile-value" id="ssid">—</div>
    </div>
    <div class="tile">
      <div class="tile-label">IP address</div>
      <div class="tile-value" id="ip">—</div>
    </div>
    <div class="tile wide">
      <div class="tile-label">WiFi signal</div>
      <div class="inline-row">
        <div class="tile-value"><span id="rssi-val">—</span><span class="unit">dBm</span></div>
        <span class="wifi-dots">
          <span id="d1" style="height:5px"></span>
          <span id="d2" style="height:8px"></span>
          <span id="d3" style="height:12px"></span>
          <span id="d4" style="height:16px"></span>
        </span>
      </div>
    </div>
    <div class="tile wide">
      <div class="tile-label">RAM</div>
      <div class="inline-row">
        <div class="mini-bar"><div class="ram-bar-fill" id="ram-bar" style="width:0%"></div></div>
      </div>
      <div class="ram-sub" id="ram-txt">—</div>
    </div>
    <div class="tile wide">
      <div class="tile-label">Requests since boot</div>
      <div class="tile-value"><span class="req-badge"><span class="req-dot"></span><span id="req-count">—</span></span></div>
    </div>
  </div>
</div>

<!-- curl -->
<div class="card curl-card">
  <div class="section-head"><div class="section-title">API · curl</div></div>
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
  <div class="curl-note"># brightness auto: -d "auto=1" &nbsp;|&nbsp; power on: -d "on=1"</div>
</div>

<!-- Chip info -->
<div class="card chip-card">
  <div class="section-head"><div class="section-title">Microcontroller · ESP32-C3 Super Mini</div></div>
  <div class="chip-grid">
    <div class="chip-item"><span class="chip-key">Architecture</span><span class="chip-val">RISC-V 32-bit</span></div>
    <div class="chip-item"><span class="chip-key">Core / Freq</span><span class="chip-val">1x up to 160 MHz</span></div>
    <div class="chip-item"><span class="chip-key">Flash</span><span class="chip-val">4 MB</span></div>
    <div class="chip-item"><span class="chip-key">SRAM</span><span class="chip-val">400 KB</span></div>
    <div class="chip-item"><span class="chip-key">WiFi</span><span class="chip-val">802.11 b/g/n 2.4 GHz</span></div>
    <div class="chip-item"><span class="chip-key">Bluetooth</span><span class="chip-val">BLE 5.0</span></div>
    <div class="chip-item"><span class="chip-key">Display</span><span class="chip-val">SSD1322 256x64 SPI</span></div>
    <div class="chip-item"><span class="chip-key">Protocol</span><span class="chip-val">REST + WebSocket :81</span></div>
  </div>
</div>

<div class="status-bar">
  <span class="ws-indicator"><span class="ws-dot" id="ws-dot"></span><span id="ws-label">CONNECTING</span></span>
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
