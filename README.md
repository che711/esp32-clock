# ESP32 Clock ⏰

Modern Wi-Fi clock based on the **ESP32-C3 Super Mini** with a large 256×64 SSD1322 OLED display.

Clean minimalist firmware, a stylish live web dashboard, REST API and WebSocket support — plus a built-in stopwatch that stays in sync between the OLED and the browser.

![OLED Display](https://github.com/che711/esp32-clock/blob/develop/assets/preview-oled.jpg)
*(Add your OLED photo here)*

## ✨ Features

- Large, crisp real-time clock on the OLED
- Automatic timezone/DST handling via a POSIX `TZ` string (no manual summer/winter switch)
- Automatic brightness by time of day, plus manual control and full display off
- Stylish live web interface (WebSocket) with a seconds progress bar and light/dark themes
- Stopwatch with lap times — synced from the device, so a page reload picks up the real running state
- Detailed device stats (die temperature, averaged CPU load, RAM, Wi-Fi, uptime)
- Full REST API + WebSocket for smart-home integrations
- `clock.local` access via mDNS
- Native unit tests (run without hardware) and CI with GitHub Actions

## 🛠️ Hardware

- **Microcontroller**: ESP32-C3 Super Mini
- **Display**: SSD1322 256×64 (4-wire software SPI)
- **Default pins**:

  | Signal | GPIO |
  |--------|------|
  | CLK    | 6    |
  | DIN    | 7    |
  | CS     | 10   |
  | DC     | 1    |
  | RST    | 3    |

**3D printed case**: [Download on MakerWorld](https://makerworld.com/ru/models/1327654-3-12-256x64-oled-display-enclosure#profileId-1365322)

## 🚀 Quick start

**1. Clone**

```bash
git clone https://github.com/che711/esp32-clock.git
cd esp32-clock
```

**2. Set your Wi-Fi and timezone** in `src/main.cpp` (2.4 GHz only — the C3 has no 5 GHz radio):

```cpp
const char* WIFI_SSID = "your_network";
const char* WIFI_PASS = "your_password";

// POSIX TZ string. Default is Central Europe (Warsaw).
// London:  "GMT0BST,M3.5.0/1,M10.5.0"
// Kyiv:    "EET-2EEST,M3.5.0/3,M10.5.0/4"
// UTC:     "UTC0"
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";
```

**3. Build & flash**

```bash
pio run                 # build
pio run -t upload       # flash
pio device monitor      # open serial monitor, then press RST on the board
```

On a good boot you should see:

```
=== ESP32-C3 Clock boot ===
Connecting to <SSID>...
IP: 192.168.x.x
HTTP :80  WS :81
[hb] up=5s wifi=OK ip=192.168.x.x rssi=-58 heap=...
```

**4. Open the dashboard** at `http://clock.local` (mDNS) or the IP printed above.

## ⚠️ ESP32-C3 Super Mini: serial monitor shows nothing

The Super Mini's USB-C port is wired to the chip's **native USB Serial/JTAG**, not to a separate UART chip. By default the Arduino core sends `Serial` to hardware UART0 (GPIO20/21), which is not connected to the USB port — so the monitor stays empty even though the firmware runs fine.

The fix is already in `platformio.ini` — these flags route `Serial` to the native USB CDC:

```ini
build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
```

Editing `platformio.ini` does **not** change the chip — you must rebuild and re-flash for the flags to take effect:

```bash
pio run -t clean
pio run -t upload
pio device monitor      # then press RST
```

## 🧰 Troubleshooting commands

```bash
# List serial ports (C3 shows up as "USB JTAG/serial debug unit", VID 303A)
pio device list

# Confirm the USB CDC flag is really compiled into the binary
pio run -t clean
pio run -v 2>&1 | grep -m1 "main.cpp.*ARDUINO_USB_CDC_ON_BOOT"

# Force a clean rebuild and re-flash
pio run -t clean
pio run -t upload

# Monitor on an explicit port/baud (port may re-enumerate after flashing)
pio device monitor -p /dev/ttyACM0 -b 115200
```

**Manual download mode** — if `upload` can't grab the port, force the bootloader:
hold **BOOT**, tap **RST**, release **BOOT**, then run `pio run -t upload`.

**Build fails with `riscv32-esp-elf-g++: not found` (Error 127)** — this is a toolchain/environment issue, not the code:

- The toolchain binary is missing (partial download / no disk space):

  ```bash
  df -h ~/.platformio
  rm -rf ~/.platformio/packages/toolchain-riscv32-esp*
  rm -rf .pio
  pio run                 # re-downloads the toolchain
  ```

- Building inside an **Alpine/musl** Docker image: the prebuilt (glibc) toolchain can't run there and reports "not found". Use a Debian/Ubuntu-based image instead (e.g. `python:3.12-slim` + `pip install platformio`).

## 🌐 Web UI & API

The device serves a single-page dashboard on port **80** and pushes live updates over **WebSocket** on port **81**.

REST endpoints:

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/api/time`       | Current time, date, uptime, epoch |
| GET  | `/api/stats`      | Full device state (JSON) |
| POST | `/api/brightness` | `value=0..100` (manual) or `auto=1` |
| POST | `/api/power`      | `on=1` / `on=0` |
| POST | `/api/reboot`     | Restart the device |

WebSocket stopwatch commands (sent as text): `sw:start`, `sw:pause`, `sw:reset`.

Examples:

```bash
curl http://clock.local/api/stats
curl -X POST http://clock.local/api/brightness -d "value=80"
curl -X POST http://clock.local/api/brightness -d "auto=1"
curl -X POST http://clock.local/api/power -d "on=0"
```

## 🧪 Native tests

The helper logic in `clock_utils.h` (time/uptime/stopwatch formatting, brightness scaling, RSSI level) is covered by unit tests that run on the host — no board required:

```bash
pio test -e native
```

## 📁 Project layout

```
├─ platformio.ini      # envs: esp32-c3-super-mini (+ USB CDC flags), native
├─ src/
│  ├─ main.cpp         # firmware: clock, web server, WebSocket, stopwatch
│  ├─ web_ui.h         # dashboard (HTML/CSS/JS in PROGMEM)
│  └─ clock_utils.h    # pure helpers (unit-tested)
└─ test/
   └─ test_native/     # Unity tests
```

## 🌡️ Power & heat

`temperatureRead()` reports the **on-die** temperature, not the room. With Wi-Fi active the C3 self-heats, so an internal reading around **45–55°C** at normal room temperature is expected — it is not a fault or a sign of a busy loop. The internal sensor is also known to read high. Contributors: the RF/Wi-Fi subsystem and CPU, and (when powered from 5V USB) the on-board AMS1117 LDO dropping 5V→3.3V right next to the chip.

Firmware steps already applied to keep it cooler:

- **CPU at 80 MHz** (`setCpuFrequencyMhz(80)`) instead of 160 — plenty for a clock + web server, and the single biggest lever on die heat. 80 MHz is the minimum at which Wi-Fi still works.
- **Wi-Fi modem sleep** (`WiFi.setSleep(true)`) — the radio sleeps between beacons.

The dashboard shows an **averaged** CPU load ("Load · avg"). An instantaneous figure swings wildly because Wi-Fi/TCP background work is bursty, so it is measured via a FreeRTOS idle-hook and smoothed with an EMA (τ ≈ 7 s) into a stable, meaningful number — normally low single digits for a clock. The die-temperature read is cached (every 10 s) to avoid a periodic blocking call.

Hardware option: powering the board via the **3.3V pin** (bypassing the LDO) runs noticeably cooler than 5V USB. For accurate *ambient* temperature, use an external sensor (e.g. BMP280) — the die sensor is not meant for that.

## ♻️ Reliability

- **Auto-reconnect**: if Wi-Fi drops, the firmware reconnects and refreshes the cached IP (checked every 10 s).
- **NTP resync** every 6 hours keeps the clock from drifting over long uptimes.

## 📋 Notes

- `ESPmDNS` is part of the arduino-esp32 core — no extra `lib_deps` needed for `clock.local`.
- Brightness is on a single 0–255 contrast scale: manual `100%` = 255, and the reported percentage matches it.
- The debug heartbeat (`[hb] ...`) and the longer boot delay in `setup()` exist so the monitor always shows life; trim them once everything works.
