### Simple api data

## Getting time
curl http://<ip_clock>/api/time

## Formated time
curl -s http://<ip_clock>/api/time | python3 -m json.tool

## Only time in the line
curl -s http://<ip_clock>/api/time | grep -o '"time":"[^"]*"'

## Full stats
curl http://<ip_clock>/api/stats

## Reboot of the clok:
curl http://<ip_clock>//api/reboot

## Brightess:
curl -X POST http://<ip_clock>/api/brightness -d "value=50" or "auto=1"


## Веб-интерфейс

Открой в браузере: `http://<IP-адрес>/`

IP-адрес отображается на самом дисплее внизу экрана сразу после подключения к WiFi.

### Возможности интерфейса

- **Живое время** — обновляется через WebSocket, задержка < 100мс
- **Статистика** — температура чипа, RAM, WiFi сигнал, CPU load, uptime, счётчик запросов
- **Яркость** — режим Auto (по времени суток) или Manual (слайдер 0–100%)
- **Питание** — кнопка включения/выключения OLED без перезагрузки
- **Reboot** — перезагрузка ESP32 из браузера
- **Тема** — тёмная/светлая, сохраняется в localStorage

### Автояркость

| Время | Яркость | Уровень |
|-------|---------|---------|
| 22:00 – 06:00 | 7% | Night |
| 06:00 – 08:00 | 40% | Morning |
| 08:00 – 20:00 | 100% | Day |
| 20:00 – 22:00 | 60% | Evening |

## REST API

### `GET /api/time`

Возвращает текущее время и дату.

```bash
curl http://192.168.1.33/api/time
```

```json
{
  "time": "14:32:05",
  "date": "10 MAY 2026",
  "day": "Sunday",
  "uptime": "02h 15m 30s",
  "timestamp": 1746878325
}
```

### `GET /api/stats`

Полная статистика устройства.

```bash
curl http://192.168.1.33/api/stats
```

```json
{
  "time": "14:32:05",
  "date": "10 MAY 2026",
  "day": "Sunday",
  "uptime": "02h 15m 30s",
  "ssid": "MyWiFi",
  "ip": "192.168.1.33",
  "rssi": -62,
  "temp": "53.0",
  "cpu": 3,
  "ram_free": 189540,
  "ram_total": 292984,
  "brightness_pct": 78,
  "brightness_label": "Day",
  "brightness_manual": false,
  "display_on": true,
  "requests": 142
}
```

### `POST /api/brightness`

Управление яркостью дисплея.

```bash
# Ручной режим, яркость 50%
curl -X POST http://192.168.1.33/api/brightness -d "value=50"

# Вернуть автоматический режим
curl -X POST http://192.168.1.33/api/brightness -d "auto=1"
```

### `POST /api/power`

Включение/выключение OLED.

```bash
# Выключить дисплей
curl -X POST http://192.168.1.33/api/power -d "on=0"

# Включить дисплей
curl -X POST http://192.168.1.33/api/power -d "on=1"
```

### `POST /api/reboot`

Перезагрузка устройства.

```bash
curl -X POST http://192.168.1.33/api/reboot
```

## WebSocket

Устройство пушит JSON-обновления каждую секунду на `ws://<IP>:81/`.

```javascript
const ws = new WebSocket('ws://192.168.1.33:81/');
ws.onmessage = (e) => {
  const data = JSON.parse(e.data);
  console.log(data.time); // "14:32:05"
};
```

## Тесты

Нативные тесты запускаются на PC без железа:

```bash
# Запуск тестов
pio test -e native

# Сборка прошивки
pio run -e esp32-c3-super-mini
```

Тесты покрывают: форматирование времени и даты, расчёт uptime, автояркость
по часу, уровни WiFi сигнала, парсинг JSON-ключей.

## CI/CD

GitHub Actions запускает при каждом push/PR:

1. **Native Tests** — 28 тестов бизнес-логики на PC
2. **Build Firmware** — компиляция прошивки для ESP32-C3 (только если тесты прошли)
3. **Lint** — статический анализ кода через cppcheck

Скомпилированная прошивка сохраняется как артефакт на 30 дней.

## Лицензия

MIT
