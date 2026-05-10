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
  }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Inter', system-ui, sans-serif;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 28px 16px;
    gap: 12px;
    transition: background 0.3s, color 0.3s;
  }

  .card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 18px;
    width: min(540px, 100%);
    transition: background 0.3s, border-color 0.3s;
  }

  /* ── Кнопка темы ── */
  .theme-btn {
    position: fixed; top: 14px; right: 14px;
    width: 40px; height: 40px;
    border-radius: 50%;
    background: var(--card); border: 1px solid var(--border);
    color: var(--text-dim); font-size: 18px;
    cursor: pointer; display: flex;
    align-items: center; justify-content: center;
    z-index: 50; transition: background 0.2s, border-color 0.2s;
  }
  .theme-btn:hover { border-color: var(--accent); color: var(--accent); }

  /* ── Часы ── */
  .clock-card { padding: 30px 24px 24px; text-align: center; overflow: hidden; }

  #time {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(44px, 10vw, 82px);
    font-weight: 700; letter-spacing: -1px;
    line-height: 1; color: var(--text); white-space: nowrap;
  }

  .divider { border: none; border-top: 1px solid var(--border); margin: 18px 0 14px; }

  #date {
    font-size: clamp(15px, 3.5vw, 20px); font-weight: 600;
    color: var(--text-dim); letter-spacing: 3px; text-transform: uppercase;
  }

  #day {
    font-size: clamp(12px, 2.5vw, 15px); font-weight: 500;
    color: var(--text-faint); letter-spacing: 5px;
    margin-top: 5px; text-transform: uppercase;
  }

  /* ── Статистика ── */
  .stats-card { padding: 22px 24px; display: flex; flex-direction: column; gap: 18px; }

  .stats-row { display: grid; grid-template-columns: 1fr 1fr; gap: 18px; }

  .stat { display: flex; flex-direction: column; gap: 5px; min-width: 0; }
  .stat.full { grid-column: span 2; }

  .stat-label {
    font-size: 11px; font-weight: 700;
    color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase;
  }

  .stat-value {
    font-family: 'JetBrains Mono', monospace;
    font-size: 17px; font-weight: 500; color: var(--text);
    line-height: 1.3; white-space: nowrap;
    overflow: hidden; text-overflow: ellipsis;
  }

  .unit { font-size: 13px; color: var(--text-dim); margin-left: 2px; }

  /* request counter badge */
  .req-badge {
    display: inline-flex; align-items: center; gap: 6px;
  }
  .req-dot {
    width: 7px; height: 7px; border-radius: 50%;
    background: var(--accent); flex-shrink: 0;
    animation: pulse-dot 2s infinite;
  }
  @keyframes pulse-dot { 0%,100%{opacity:1;transform:scale(1)} 50%{opacity:.4;transform:scale(.7)} }

  /* WiFi dots */
  .wifi-row { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  .wifi-dots { display: inline-flex; gap: 3px; align-items: flex-end; }
  .wifi-dots span { display: inline-block; width: 6px; border-radius: 2px; background: var(--wifi-off); }
  .wifi-dots span.on { background: var(--accent); }

  /* CPU bar */
  .cpu-row { display: flex; align-items: center; gap: 10px; }
  .cpu-mini-bar { flex: 1; height: 5px; background: var(--curl-bg); border-radius: 3px; overflow: hidden; max-width: 80px; }
  .cpu-mini-fill { height: 100%; border-radius: 3px; background: var(--accent); transition: width 1s ease; }
  .cpu-mini-fill.warn { background: #d97706; }
  .cpu-mini-fill.crit { background: #dc2626; }

  /* ── Reboot ── */
  .reboot-card {
    padding: 16px 24px; display: flex;
    align-items: center; justify-content: space-between; gap: 16px;
  }
  .reboot-info { display: flex; flex-direction: column; gap: 4px; }
  .reboot-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; }
  #uptime-label { font-family: 'JetBrains Mono', monospace; font-size: 17px; color: var(--text); }

  .btn-reboot {
    background: var(--btn-bg); border: 1px solid var(--border);
    color: var(--btn-color); font-family: 'Inter', sans-serif;
    font-size: 12px; font-weight: 700; letter-spacing: 1px;
    padding: 10px 20px; border-radius: 9px; cursor: pointer;
    text-transform: uppercase;
    transition: background 0.2s, border-color 0.2s, color 0.2s;
    white-space: nowrap; flex-shrink: 0;
  }
  .btn-reboot:hover    { background: #dc2626; border-color: #dc2626; color: #fff; }
  .btn-reboot:active   { background: #b91c1c; }
  .btn-reboot:disabled { opacity: 0.4; cursor: not-allowed; }

  /* ── curl ── */
  .curl-card { padding: 18px 24px; background: var(--curl-bg); border-color: var(--curl-border); }
  .curl-label { font-size: 11px; font-weight: 700; color: var(--text-faint); letter-spacing: 1.5px; text-transform: uppercase; margin-bottom: 12px; }
  .curl-line { font-family: 'JetBrains Mono', monospace; font-size: 13px; line-height: 2.2; display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
  .curl-cmd  { color: #7ab0ff; }
  .curl-url  { color: #34d399; word-break: break-all; }
  .curl-note { color: var(--text-faint); font-size: 11px; }

  /* ── Статус ── */
  .status-bar {
    display: flex; align-items: center; gap: 14px;
    font-size: 11px; font-weight: 500;
    color: var(--text-faint); letter-spacing: 1.5px; margin-top: 4px;
  }
  .ws-indicator { display: flex; align-items: center; gap: 5px; font-size: 10px; letter-spacing: 1px; text-transform: uppercase; }
  .ws-dot { width: 7px; height: 7px; border-radius: 50%; background: #6b7280; transition: background 0.3s; }
  .ws-dot.connected    { background: #16a34a; animation: pulse 2s infinite; }
  .ws-dot.disconnected { background: #dc2626; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.25} }

  /* toast */
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
    <div class="stat-value" id="ram-txt">—</div>
  </div>

  <div class="stat">
    <div class="stat-label">Display Brightness</div>
    <div class="stat-value">
      <span id="brightness-pct">—</span><span class="unit">%</span>
      <span style="font-size:12px;color:var(--text-faint);margin-left:8px" id="brightness-label"></span>
    </div>
  </div>

  <div class="stat">
    <div class="stat-label">Requests Since Boot</div>
    <div class="stat-value">
      <div class="req-badge">
        <span class="req-dot"></span>
        <span id="req-count">—</span>
      </div>
    </div>
  </div>

</div>

<!-- Reboot -->
<div class="card reboot-card">
  <div class="reboot-info">
    <div class="reboot-label">Uptime</div>
    <div id="uptime-label">—</div>
  </div>
  <button class="btn-reboot" id="btn-reboot" onclick="reboot()">Reboot</button>
</div>

<!-- curl -->
<div class="card curl-card">
  <div class="curl-label">API · curl examples</div>
  <div class="curl-line"><span class="curl-cmd">curl</span><span class="curl-url" id="curl-time">http://&lt;IP&gt;/api/time</span></div>
  <div class="curl-line"><span class="curl-cmd">curl</span><span class="curl-url" id="curl-stats">http://&lt;IP&gt;/api/stats</span></div>
  <div class="curl-line curl-note"># JSON: time, date, day, uptime, temp, rssi, ram, cpu, brightness, requests</div>
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
  const savedTheme = localStorage.getItem('theme') || 'dark';
  applyTheme(savedTheme);

  function applyTheme(t) {
    document.documentElement.setAttribute('data-theme', t);
    document.getElementById('theme-btn').textContent = t === 'dark' ? '🌙' : '☀️';
  }
  function toggleTheme() {
    const cur  = document.documentElement.getAttribute('data-theme');
    const next = cur === 'dark' ? 'light' : 'dark';
    applyTheme(next);
    localStorage.setItem('theme', next);
  }

  // ── WebSocket ────────────────────────────────────────
  let ws, wsReconnectTimer;

  function wsConnect() {
    ws = new WebSocket(`ws://${location.hostname}:81/`);
    ws.onopen  = () => { setWsStatus(true);  clearTimeout(wsReconnectTimer); };
    ws.onmessage = (e) => { try { updateUI(JSON.parse(e.data)); } catch(_) {} };
    ws.onclose = () => { setWsStatus(false); wsReconnectTimer = setTimeout(wsConnect, 3000); };
    ws.onerror = () => ws.close();
  }

  function setWsStatus(ok) {
    document.getElementById('ws-dot').className   = 'ws-dot ' + (ok ? 'connected' : 'disconnected');
    document.getElementById('ws-label').textContent = ok ? 'LIVE' : 'OFFLINE';
  }

  // ── UI update ────────────────────────────────────────
  function updateUI(d) {
    document.getElementById('time').textContent            = d.time;
    document.getElementById('date').textContent            = d.date;
    document.getElementById('day').textContent             = d.day;
    document.getElementById('uptime-label').textContent    = d.uptime;
    document.getElementById('ssid').textContent            = d.ssid;
    document.getElementById('ip').textContent              = d.ip;
    document.getElementById('temp').textContent            = d.temp;
    document.getElementById('rssi-val').textContent        = d.rssi;
    document.getElementById('cpu-val').textContent         = d.cpu;
    document.getElementById('brightness-pct').textContent  = d.brightness_pct;
    document.getElementById('brightness-label').textContent = d.brightness_label;
    document.getElementById('curl-time').textContent       = 'http://' + d.ip + '/api/time';
    document.getElementById('curl-stats').textContent      = 'http://' + d.ip + '/api/stats';
    document.getElementById('req-count').textContent       = d.requests.toLocaleString();

    // CPU bar
    const cpuEl = document.getElementById('cpu-bar');
    cpuEl.style.width = d.cpu + '%';
    cpuEl.className = 'cpu-mini-fill' + (d.cpu>80?' crit':d.cpu>50?' warn':'');

    // WiFi dots
    const lvl = d.rssi>=-50?4:d.rssi>=-60?3:d.rssi>=-70?2:1;
    for(let i=1;i<=4;i++)
      document.getElementById('d'+i).className = i<=lvl?'on':'';

    // RAM
    const used = d.ram_total - d.ram_free;
    const pct  = Math.round(used * 100 / d.ram_total);
    document.getElementById('ram-txt').textContent =
      (d.ram_free/1024).toFixed(1) + ' KB free / ' +
      (d.ram_total/1024).toFixed(1) + ' KB · ' + pct + '% used';
  }

  // ── Reboot ───────────────────────────────────────────
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
