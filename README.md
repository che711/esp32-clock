# ESP32 Clock ⏰

Modern Wi-Fi clock based on **ESP32-C3 Super Mini** with a large 256×64 SSD1322 OLED display.

Clean minimalist design, excellent readability, powerful web interface, REST API and WebSocket support.

![OLED Display](https://github.com/che711/esp32-clock/blob/develop/assets/preview-oled.jpg)  
*(Add your OLED photo here)*

## ✨ Features

- Large, crisp real-time clock on OLED
- Automatic brightness adjustment based on time of day
- Manual brightness + complete display off
- Beautiful live web interface with instant WebSocket updates
- Detailed device statistics (temperature, RAM, WiFi, CPU, uptime)
- Full REST API + WebSocket for smart home integrations
- Native tests (run without hardware)
- CI/CD with GitHub Actions

## 📸 Screenshots

### Web Interface

![Web Interface - Auto Mode](https://github.com/che711/esp32-clock/blob/develop/assets/web-interface-auto.png)
![Web Interface - Manual Mode](https://github.com/che711/esp32-clock/blob/develop/assets/web-interface-manual.png)

## 🛠️ Hardware

- **Microcontroller**: ESP32-C3 Super Mini
- **Display**: SSD1322 256×64 (4-wire SPI)
- **Default pins**:
  - CLK → GPIO6
  - DIN → GPIO7
  - CS  → GPIO10
  - DC  → GPIO1
  - RST → GPIO3

**3D Printed Case**:  
[Download case on MakerWorld](https://makerworld.com/ru/models/1327654-3-12-256x64-oled-display-enclosure#profileId-1365322)

## 🚀 Quick Start

1. Clone the repository:
   ```bash
   git clone https://github.com/che711/esp32-clock.git
   cd esp32-clock
   