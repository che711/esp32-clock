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

  body {
    background: #0f2a5c;
    color: #cfe0ff;
    font-family: 'Inter', system-ui, sans-serif;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: flex-start;
    padding: 28px 16px 32px;
    gap: 12px;
  }

  .card {
    background: #0a1f47;
    border: 1px solid #1a3a70;
    border-radius: 18px;
    width: 100%;
    max-width: 460px;
  }

  /* ── Часы ── */
  .clock-card {
    padding: 28px 24px 22px;
    text-align: center;
  }

  #time {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(48px, 13vw, 90px);
    font-weight: 700;
    letter-spacing: -1px;
    line-height: 1;
    color: #fff;
    white-space: nowrap;
    overflow: hidden;
  }

  .divider {
    border: none;
    border-top: 1px solid #1a3a70;
    margin: 16px 0 12px;
  }

  #date {
    font-size: clamp(13px, 3.5vw, 18px);
    font-weight: 600;
    color: #4d7ec4;
    letter-spacing: 3px;
    text-transform: uppercase;
  }

  #day {
    font-size: clamp(11px, 2.5vw, 14px);
    font-weight: 500;
    color: #2a4e8a;
    letter-spacing: 4px;
    margin-top: 5px;
    text-transform: uppercase;
  }

  /* ── Статистика ── */
  .stats-card {
    padding: 20px 20px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }

  .stats-row {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 14px;
  }

  .stat {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .stat-label {
    font-size: 10px;
    font-weight: 700;
    color: #2a4e8a;
    letter-spacing: 1.5px;
    text-transform: uppercase;
  }

  .stat-value {
    font-family: 'JetBrains Mono', monospace;
    font-size: 15px;
    font-weight: 500;
    color: #fff;
    line-height: 1.3;
  }

  .unit {
    font-size: 12px;
    color: #4d7ec4;
    margin-left: 2px;
  }

  /* WiFi dots */
  .wifi-row { display: flex; align-items: center; gap: 8px; }
  .wifi-dots { display: inline-flex; gap: 3px; align-items: flex-end; }
  .wifi-dots span {
    display: inline-block; width: 5px; border-radius: 2px; background: #1a3a70;
  }
  .wifi-dots span.on { background: #4d8ef0; }

  /* CPU bar mini */
  .cpu-row { display: flex; align-items: center; gap: 10px; }
  .cpu-mini-bar {
    flex: 1; height: 4px; background: #0a1525;
    border-radius: 2px; overflow: hidden; max-width: 80px;
  }
  .cpu-mini-fill {
    height: 100%; border-radius: 2px; background: #4d8ef0;
    transition: width 1s ease;
  }
  .cpu-mini-fill.warn { background: #d97706; }
  .cpu-mini-fill.crit { background: #dc2626; }

  /* ── curl блок ── */
  .curl-card {
    padding: 16px 20px;
    background: #060e20;
    border-color: #132845;
  }

  .curl-label {
    font-size: 10px;
    font-weight: 700;
    color: #2a4e8a;
    letter-spacing: 1.5px;
    text-transform: uppercase;
    margin-bottom: 10px;
  }

  .curl-line {
    font-family: 'JetBrains Mono', monospace;
    font-size: 12px;
    line-height: 2;
    display: flex;
    flex-wrap: wrap;
    gap: 6px;
    align-items: center;
  }

  .curl-cmd  { color: #7ab0ff; }
  .curl-url  { color: #34d399; word-break: break-all; }
  .curl-note { color: #2a4e8a; font-size: 11px; }

  /* ── Reboot ── */
  .reboot-card {
    padding: 14px 20px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .reboot-info {
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .reboot-label {
    font-size: 10px;
    font-weight: 700;
    color: #2a4e8a;
    letter-spacing: 1.5px;
    text-transform: uppercase;
  }

  #uptime-label {
    font-family: 'JetBrains Mono', monospace;
    font-size: 14px;
    color: #fff;
  }

  .btn-reboot {
    background: #1a2e5c;
    border: 1px solid #2a4e8a;
    color: #7ab0ff;
    font-family: 'Inter', sans-serif;
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 1px;
    padding: 8px 18px;
    border-radius: 8px;
    cursor: pointer;
    text-transform: uppercase;
    transition: background 0.2s, border-color 0.2s, color 0.2s;
    white-space: nowrap;
  }

  .btn-reboot:hover  { background: #dc2626; border-color: #dc2626; color: #fff; }
  .btn-reboot:active { background: #b91c1c; }
  .btn-reboot:disabled { opacity: 0.4; cursor: not-allowed; }

  /* статус */
  .status {
    font-size: 10px;
    font-weight: 500;
    color: #1a3a70;
    letter-spacing: 2px;
    margin-top: 4px;
  }
  .dot {
    display: inline-block; width: 6px; height: 6px;
    border-radius: 50%; background: #16a34a;
    margin-right: 6px; vertical-align: middle;
    animation: pulse 2s infinite;
  }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.2} }

  /* toast */
  .toast {
    position: fixed; bottom: 24px; left: 50%; transform: translateX(-50%);
    background: #1e3a6e; border: 1px solid #2a5298;
    color: #cfe0ff; font-size: 13px; font-weight: 500;
    padding: 10px 22px; border-radius: 10px;
    opacity: 0; transition: opacity 0.3s;
    pointer-events: none; white-space: nowrap;
  }
  .toast.show { opacity: 1; }
</style>
</head>
<body>

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
        <span id="d1" style="height:4px"></span>
        <span id="d2" style="height:7px"></span>
        <span id="d3" style="height:11px"></span>
        <span id="d4" style="height:15px"></span>
      </span>
    </div>
  </div>

  <div class="stat">
    <div class="stat-label">RAM</div>
    <div class="stat-value" id="ram-txt">—</div>
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
  <div class="curl-line">
    <span class="curl-cmd">curl</span>
    <span class="curl-url" id="curl-time">http://&lt;IP&gt;/api/time</span>
  </div>
  <div class="curl-line">
    <span class="curl-cmd">curl</span>
    <span class="curl-url" id="curl-stats">http://&lt;IP&gt;/api/stats</span>
  </div>
  <div class="curl-line curl-note"># JSON: time, date, day, uptime, temp, rssi, ram, cpu</div>
</div>

<div class="status"><span class="dot"></span>ESP32-C3 SUPER MINI</div>
<div class="toast" id="toast"></div>

<script>
  function showToast(msg, ms=2500) {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.classList.add('show');
    setTimeout(() => t.classList.remove('show'), ms);
  }

  async function reboot() {
    if (!confirm('Reboot the clock?')) return;
    const btn = document.getElementById('btn-reboot');
    btn.disabled = true;
    btn.textContent = 'Rebooting…';
    try {
      await fetch('/api/reboot', { method: 'POST' });
      showToast('Rebooting… reconnecting in 5s', 5000);
      setTimeout(() => { btn.disabled=false; btn.textContent='Reboot'; }, 6000);
    } catch(e) {
      showToast('Sent — device is restarting');
      setTimeout(() => { btn.disabled=false; btn.textContent='Reboot'; }, 6000);
    }
  }

  function setCpuBar(pct) {
    const el = document.getElementById('cpu-bar');
    el.style.width = pct + '%';
    el.className = 'cpu-mini-fill' + (pct>80?' crit':pct>50?' warn':'');
  }

  function setWifi(rssi) {
    const lvl = rssi>=-50?4:rssi>=-60?3:rssi>=-70?2:1;
    for(let i=1;i<=4;i++)
      document.getElementById('d'+i).className = i<=lvl?'on':'';
  }

  async function tick() {
    try {
      const d = await (await fetch('/api/stats')).json();
      document.getElementById('time').textContent   = d.time;
      document.getElementById('date').textContent   = d.date;
      document.getElementById('day').textContent    = d.day;
      document.getElementById('uptime-label').textContent = d.uptime;
      document.getElementById('ssid').textContent   = d.ssid;
      document.getElementById('ip').textContent     = d.ip;
      document.getElementById('temp').textContent   = d.temp;
      document.getElementById('rssi-val').textContent = d.rssi;
      document.getElementById('cpu-val').textContent  = d.cpu;
      document.getElementById('curl-time').textContent  = 'http://' + d.ip + '/api/time';
      document.getElementById('curl-stats').textContent = 'http://' + d.ip + '/api/stats';
      setCpuBar(d.cpu);
      setWifi(d.rssi);
      const used = d.ram_total - d.ram_free;
      const pct  = Math.round(used * 100 / d.ram_total);
      document.getElementById('ram-txt').textContent =
        (d.ram_free/1024).toFixed(1) + ' KB free / ' +
        (d.ram_total/1024).toFixed(1) + ' KB · ' + pct + '% used';
    } catch(e) {}
  }
  tick();
  setInterval(tick, 2000);
</script>
</body>
</html>
)rawliteral";
