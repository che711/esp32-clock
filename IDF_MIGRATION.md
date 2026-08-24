# Переезд на ESP-IDF

Бриф для нового диалога: что уже решено, почему, и с чего начинать. Всё ниже
установлено замерами и проверкой окружения — выводить заново не надо.

## Зачем

Часы живут на 18650 около суток. Самая крупная статья расхода — 60–80 мА,
которые тянут чип с радио: процессор не спит вообще, `loop()` крутится на
`delay()` при 80 МГц, Wi-Fi ассоциирован. Light sleep способен снять с этого
10–45 мА, то есть дать от +3 до +22 часов (точная цифра зависит от того,
сколько в этих 60–80 мА приходится на процессор, а сколько на обвязку
OLED-модуля и маячки радио — это отдельный замер, см. «Что померить»).

Включить его в нынешней сборке нельзя: в готовых библиотеках pioarduino
`# CONFIG_PM_ENABLE is not set`, а без него `esp_pm_configure()` возвращает
`ESP_ERR_NOT_SUPPORTED`.

Второй мотив, не менее весомый: официальная платформа `espressif32` не
поддерживает C6 вовсе, проект держится на форке pioarduino. IDF — upstream.

## Что решено

**Цель — чистый ESP-IDF, без Arduino.** Не «Arduino как компонент IDF».

Arduino-компонентом переезжать бессмысленно: arduino-esp32 3.3.9 намертво
привязан к IDF 5.5.4, то есть локальный 6.1 всё равно не подходит, Arduino-слой
остаётся, а сверху добавляется по `CMakeLists.txt` на каждую библиотеку.
Взамен не даётся ничего, чего не даёт `custom_sdkconfig` в platformio.ini.

**PM через `custom_sdkconfig` — временная мера, а не альтернатива.** Если
light sleep нужен раньше, чем закончится переезд, он включается в нынешней
сборке пятью строками (см. «Короткий путь»). Переезду это не мешает и после
него выкидывается.

## Окружение (проверено)

| Что | Состояние |
| --- | --- |
| `~/esp/esp-idf` | v6.1-dev, полный клон (не shallow), теги v5.5.x на месте |
| `~/.espressif/tools` | riscv32-esp-elf, gdb, openocd-esp32 v0.12.0 |
| `~/.espressif/python_env/idf6.1_py3.12_env` | esp-coredump 1.16.0 |
| pioarduino 55.03.39 | Arduino core 3.3.9 → требует IDF **5.5.4** |
| Плата | ESP32-C6-Zero, 4 МБ флеша, USB-JTAG встроен |

Для чистого IDF годится имеющийся 6.1 — второй копии не нужно. Версия 5.5.4
понадобилась бы только для Arduino-компонента или для `custom_sdkconfig`.

## Что нельзя сломать

1. **146 нативных тестов** (`pio test -e native`) — это приёмка переезда.
   Вся логика в `*_calc.h` уже без Arduino и переезжает даром; при переносе
   её **не переписывать**. Если тест пришлось править — значит, поведение
   поехало, и это ошибка переноса, а не улучшение.
2. **`web/index.html` — единственный источник UI.** В IDF гзип и вычисление
   ETag переезжают в `add_custom_command` + `EMBED_FILES`. Формат ответа
   `/api/*` и полей WebSocket-снимка менять нельзя: дашборд читает их по имени.
3. **Таблица разделов.** `huge_app.csv` даёт app на 3 МБ и раздел `coredump`
   на 64 КБ. Core dump уже включён и работает — сохранить.
4. **USB CDC на загрузке** (`ARDUINO_USB_MODE=1`, `CDC_ON_BOOT=1`): на C6-Zero
   USB-C идёт на нативный USB, без этого монитор пуст. В IDF это
   `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`.
5. **Поведение при разряде.** Пороги из `config.h` (гашение на 5 %, deep sleep
   на 0 %, пробуждение на 10 %) и кривая разряда — не трогать, они выверены.

## Порядок работ

Одна ветка, коммит на шаг, тесты зелёные после каждого.

1. **Каркас.** `CMakeLists.txt`, `main/`, `sdkconfig.defaults`, `partitions.csv`.
   Пустой `app_main()`, который мигает LED. Цель шага — чтобы собиралось и
   прошивалось.
2. **Нативные тесты вне PlatformIO.** Голый cmake + unity либо `idf.py
   --preview set-target linux`. Пока тесты не запускаются, дальше идти нельзя:
   без них переезд не проверить.
3. **`web_api.cpp` — самый крупный кусок**, начинать с него: станет видно
   цену остального. `WebServer` → `esp_http_server`, links2004/WebSockets →
   встроенный в него WebSocket. Снимок JSON собирается тем же `snprintf`.
4. **Сеть в `main.cpp`:** `WiFi.h` → `esp_wifi` + `esp_netif`, `ESPmDNS` →
   компонент `mdns`, `configTzTime` → `esp_netif_sntp`.
5. **Драйверы:** BMP280 (свой на `i2c_master`, ~150 строк, или компонент из
   реестра), АЦП батареи → `adc_oneshot` + `adc_cali`, WS2812 → `led_strip`,
   U8g2 → HAL-порт под IDF.
6. **MQTT:** PubSubClient → `esp-mqtt`. Выключен по умолчанию, поэтому последним.
7. **Сборка UI:** `scripts/gen_web_ui.py` → `add_custom_command` + `EMBED_FILES`.
8. **PM:** `CONFIG_PM_ENABLE=y`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`,
   `CONFIG_ESP_WIFI_SLP_BEACON_LOST_OPT=y`, вызов `esp_pm_configure()`
   (`max_freq_mhz = 80`, `min_freq_mhz = 40`, `light_sleep_enable = true`).
9. **CI:** `.github/workflows/ci.yml` — сборка на `espressif/esp-idf-ci-action`,
   тесты на своём cmake вместо `pio test`.

## Таблица замен

| Сейчас | В IDF |
| --- | --- |
| `WiFi.h`, `ESPmDNS` | `esp_wifi` + `esp_netif` + компонент `mdns` |
| `WebServer` | `esp_http_server` |
| links2004/WebSockets | WebSocket внутри `esp_http_server` |
| PubSubClient | `esp-mqtt` (`mqtt_client.h`) |
| U8g2 | HAL-порт под IDF |
| Adafruit BMP280 + Unified Sensor | свой драйвер на `i2c_master` |
| `analogReadMilliVolts()` | `adc_oneshot` + `adc_cali` |
| `rgbLedWrite()` | компонент `led_strip` (RMT) |
| `millis()` | `esp_timer_get_time() / 1000` |
| `setup()` / `loop()` | `app_main()` + задача цикла |

## Приёмка

- `pio test -e native` (или его замена) — 146 тестов зелёные, ни один не правлен.
- Дашборд открывается, ползунок яркости и секундомер работают, снимок по
  WebSocket идёт раз в секунду.
- Ток на ЛБП при погашенном экране **ниже нынешних 60–80 мА** — ради этого
  всё и затевалось. Не стало ниже — PM не работает, разбираться до конца.

## Что померить (не требует переезда)

`esp_light_sleep_start()` и `esp_deep_sleep_start()` доступны в нынешней
сборке: `CONFIG_PM_ENABLE` нужен только для автоматического сна. Отсюда два
замера, снимающие неопределённость «+3 или +22 часа»:

1. **Deep sleep на 30 с** — абсолютный пол: сколько тянет плата с подключённым
   модулем экрана при неработающем процессоре. Ниже этого light sleep не даст.
2. **Light sleep на 10 с с живым Wi-Fi** — ровно тот ток, к которому придёт
   прошивка после PM. Разница с нынешними 60–80 мА и есть выигрыш.

## Короткий путь, если PM нужен раньше переезда

```bash
git -C ~/esp/esp-idf worktree add ~/esp/idf-5.5.4 v5.5.4
cd ~/esp/idf-5.5.4 && git submodule update --init --recursive && ./install.sh esp32c6
```

```ini
platform_packages = framework-espidf @ symlink:///home/andrew/esp/idf-5.5.4
custom_sdkconfig =
    CONFIG_PM_ENABLE=y
    CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
    CONFIG_ESP_WIFI_SLP_BEACON_LOST_OPT=y
```

Команды трогают `~/esp` — это вне проекта, выполняет их владелец, не агент.
