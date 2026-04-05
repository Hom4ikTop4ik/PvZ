# Контекст проекта PvZ

Ниже собран актуальный контекст проекта с фокусом только на последних и релевантных версиях прошивки и текущем направлении переноса sensor-node на STM32L431CCT6. Старые версии прошивки sensor-кода не описываются, кроме как в одном уточнении о том, что они устарели.

---

## 1. Что это за проект

Проект `github.com/Nikolay56615/PvZ` — это IoT-система для сбора телеметрии с умных датчиков и передачи её по LoRa в шлюз, который дальше отправляет данные в MQTT/Backend.

### Текущая целевая архитектура
- **sensor-node**:
  - сейчас был на ESP32/MicroPython;
  - в актуальной ветке hardware основная рабочая версия sensor-кода — `main_sensor_wroom_2.py`;
  - в дальнейшем sensor-node переносится на **STM32L431CCT6**.
- **gateway**:
  - остаётся на ESP32-C3 Super Mini;
  - принимает LoRa;
  - публикует телеметрию в MQTT;
  - принимает команды из MQTT и отправляет их обратно в LoRa.
- **backend**:
  - FastAPI + MQTT + PostgreSQL;
  - принимает MQTT-телеметрию;
  - хранит данные по tenant-ам;
  - поддерживает управление устройствами.
- **IaC / localhosting**:
  - тестовые сервисы для fake devices, retain cleaner и прочих сценариев.

---

## 2. Актуальные файлы hardware-ветки

Ниже — только релевантные последние версии.

### Sensor / LoRa
- `main_sensor_wroom_2.py` — основная актуальная прошивка sensor-node на MicroPython/ESP32
- `sensors.py` — датчики и питание
- `lora_mini_lib.py` — LoRa E22 обёртка
- `utils.py` — очередь на отправку сообщений LoRaTxQueue, history, pump
- `config_common.py` — общие настройки sensor
- `make_payload.py` — генерация payload-строк

### Gateway
- `main_gateway.py` — основная прошивка gateway
- `config_gateway.py` — настройки gateway

### STM32-переход
- CubeMX/CubeIDE проект на **STM32L431CCT6**
- пользовательская бизнес-логика должна будет выноситься в отдельные модули вроде:
  - `sensor_node.c/.h`
  - `lora_driver.c/.h`
  - `gps_driver.c/.h`
  - `hw390.c/.h`
  - `ina219.c/.h`
  - `power_ctrl.c/.h`

---

## 3. Актуальная sensor-прошивка: `main_sensor_wroom_2.py`
Это самая важная sensor-прошивка в текущем hardware-контексте.

### 3.1 Назначение
Прошивка предназначена для:
- периодического сбора телеметрии;
- контроля влажности;
- контроля температуры;
- получения GPS;
- контроля батареи;
- отправки всего по LoRa;
- приёма LoRa-команд;
- ретрансляции чужих пакетов;
- минимизации RAM-расхода через очередь сообщений.

### 3.2 Что подключается
- `config_common as config`
- `LoRaMiniLib`
- `RingBuffer`
- `LoRaTxQueue`
- `lora_tx_pump`
- `HW_Sensor`
- `DS_Sensor`
- `GPS_Sensor`
- `INA219`

### 3.3 Поведение
Прошивка работает как event/poll loop:
- проверяет, пора ли измерять влажность;
- проверяет, пора ли измерять температуру;
- проверяет, пора ли запрашивать GPS;
- проверяет, пора ли обновлять state;
- читает LoRa RX;
- использует очередь TX;
- поддерживает forced measurements;
- поддерживает команды по LoRa.

### 3.4 Включатели телеметрии
В прошивке есть флаги:
- `need_humidity_info`
- `need_temperature_info`
- `need_gps_info`
- `need_status_info`

### 3.5 Одноразовые форсирующие флаги
- `force_humidity_measure`
- `force_temperature_measure`
- `force_geo_measure`
- `force_status_measure`

### 3.6 Таймеры последнего измерения
- `_last_hw_ms`
- `_last_ds_ms`
- `_last_gps_ms`
- `_last_status_ms`

### 3.7 Логика периодов
Функции:
- `check_need_humidity_measurement()`
- `check_need_temperature_measurement()`
- `check_need_geo_measurement()`
- `check_need_status_measurement()`

Они возвращают `True`, если:
- включён соответствующий поток телеметрии;
- либо выставлен force-флаг;
- либо истёк период из `config`.

### 3.8 RTC и GPS time sync
Есть:
- `set_rtc_utc(date_ymd, time_hms)`
- `wait_utc_date_time(gps, timeout_s, log_every_ms)`

Прошивка умеет ждать UTC-время от GPS и выставлять RTC.

### 3.9 Отправка данных
Функции:
- `send_humidity()`
- `send_temperature()`
- `send_geo()`
- `send_state()`

Они **не отправляют сразу**, а кладут сообщение в LoRaTxQueue очередь.

### 3.10. Форматы сообщений

#### 3.10.1 Sensor and Gateway LoRa payload
`device_id;timestamp;msg_rnd_id;msg_type;payload`

#### 3.10.2 Типы msg_type
- `hum`
- `tmp`
- `geo`
- `stt`
- `cmd`

#### 3.10.3 Информация для формирования сообщений (payload) для Gateway MQTT 
- humidity:
  - `device_id,timestamp,humidity,seq`
- temperature:
  - `device_id,timestamp,temperature,seq`
- location:
  - `device_id,timestamp,lat,lon,seq`
- state:
  - `device_id,timestamp,rssi,snr,battery,online,seq`

#### 3.10.4 ACK payload
В `make_payload.py`:
- `command_id,timestamp,status,details`

### 3.11 Команды
`do_command()` поддерживает:
- `SLEEP`
- `FORCE_HUM`
- `FORCE_TMP`
- `FORCE_GEO`
- `FORCE_STT`

### 3.12 Обработка пакетов
`do_payload(payload, payload_bytes)`:
- разбирает payload;
- строит ключ `(device_id, timestamp, msg_rnd_id)`;
- проверяет history;
- если пакет свой и `msg_type == "cmd"` — вызывает `do_command()`;
- если пакет чужой — ретранслирует через `tx_queue`.

### 3.13 Основной цикл MicroPython версии
Прошивка:
1. опрашивает кнопку;
2. читает LoRa RX;
3. делает `gps_sensor.poll()`;
4. проверяет расписание измерений;
5. читает датчики;
6. кладёт payload в очередь;
7. вызывает `lora_tx_pump(...)`;
8. спит `time.sleep_ms(20)`.
*Важно*: Для STM32: этот цикл не переносится напрямую. STM32-версия должна использовать таймеры, прерывания и RTC wake-up, без блокирующих задержек!

---

## 4. Актуальная gateway-прошивка: `main_gateway.py`

### 4.1 Назначение
Gateway:
- слушает LoRa;
- отправляет данные в MQTT;
- принимает MQTT-команды;
- передаёт команды обратно в LoRa;
- следит за Wi-Fi и MQTT reconnect;
- делает NTP sync;
- хранит историю последних сообщений.

### 4.2 Основные части
- LED / button init;
- LoRa init;
- RingBuffer history;
- LoRaTxQueue;
- Wi-Fi connection;
- NTP sync;
- MQTT connection;
- MQTT callback;
- LoRa RX loop;
- TX pump.

### 4.3 Входящие LoRa payload
Смотрите пункт 3.10

### 4.4 Типы сообщений
Смотрите пункт 3.10

### 4.5 Публикация в MQTT
Gateway публикует в топики:
- `dev/fake/sensors/{device_id}/humidity`
- `dev/fake/sensors/{device_id}/temperature`
- `dev/fake/sensors/{device_id}/location`
- `dev/fake/sensors/{device_id}/state`

### 4.6 Подписка на команды
Gateway подписывается на:
- `dev/fake/devices/+/command`

### 4.7 Команда в MQTT
`on_message_received()`:
- берёт `topic`;
- извлекает `target_device_id`;
- разбирает payload;
- вызывает `send_command(cmd_type, *params, device_id=target_device_id, timestamp=timestamp)`.

### 4.8 Reconnect logic
- `connect_wifi()`
- `sync_time()`
- `connect_mqtt()`
- `ensure_mqtt()`

### 4.9 Обработка LoRa payload
`do_payload(payload)`:
- проверяет history;
- если `hum` — публикует humidity;
- если `tmp` — публикует temperature;
- если `geo` — публикует location;
- если `stt` — публикует state;
- если ошибка MQTT — сбрасывает клиента и переподключается.

---

## 5. Датчики и вспомогательные классы: `sensors.py`

### 5.1 `HW_Sensor`
Назначение:
- читать влажность почвы через ADC;
- усреднять несколько измерений;
- переводить raw ADC в проценты.

Ключевые методы:
- `available()`
- `read_raw()`
- `read_percent()`

### 5.2 `DS_Sensor`
Назначение:
- читать температуру DS18B20 по OneWire.

Ключевые методы:
- `available()`
- `read_temperature(index=0)`

### 5.3 `INA219`
Назначение:
- читать питание батареи и ток по I2C.

Ключевые методы:
- `configure()`
- `read_battery_voltage_v()`
- `read_percent()`
- `get_current_ma()`
- `get_power_w()`

### 5.4 `GPS_Sensor`
Назначение:
- читать NMEA по UART;
- хранить координаты;
- хранить UTC дату/время;
- уметь включать/выключать питание;
- уметь конфигурировать GPS через UBX.

Ключевые методы:
- `power_on()`
- `power_off()`
- `poll()`
- `check_ready()`
- `read_lat_lon()`
- `configure_glonass_only(save=True, cold_start=True)`

---

## 6. LoRa-обёртка: `lora_mini_lib.py`

Это MicroPython-обёртка для LoRa E22.

### 6.1 Что делает
- управляет M0/M1/AUX;
- создаёт UART;
- ожидает готовность через AUX;
- шлёт и принимает bytes;
- поддерживает normal и WOR RX режимы.

### 6.2 Ключевые методы
- `set_mode_normal()`
- `set_mode_wor_rx()`
- `wait_for_aux()`
- `send_bytes(data)`
- `receive_bytes(timeout_ms)`
- `receive_bytes_wor(timeout_ms)`
- `read_if_any()`

### 6.3 Параметры
- `MAX_PACKET_SIZE = 240`
- `ACK_TIMEOUT = 20000`
- `AUX_WAIT_TIMEOUT = 100`
- `UART_WAIT_TIMEOUT = 50`

---

## 7. Общие утилиты: `utils.py`

### 7.1 `RingBuffer`
Используется для хранения и антидублирования отправленных и пересылаемых сообщений, чтобы избегать так назыавемых broadcast-штормов.

### 7.2 `LoRaTxQueue`
Ограниченная очередь для сообщений LoRa bytes. Необходима так как сообщений может быть в моменте несколько, а единовременно в радиоэфир их отправить нельзя — нужна задержка между отправками.

### 7.3 `lora_tx_pump`
Отправляет максимум один пакет за вызов, учитывает минимальный интервал.

### 7.4 `lora_tx_pump_new`
Более продвинутая версия:
- умеет `peek/drop`;
- может вернуть `busy`.

---

## 8. Конфигурации

### 8.1 `config_common.py`
Общая конфигурация sensor:
- `NODE_ID` (менялось от устройства к устройству вручную, hardcoded)
- LoRa pins
- GPS pins
- INA219 pins
- HW390 pins
- calibration for wet/dry
- telemetry periods
- queue sizes
- history size

### 8.2 `config_gateway.py`
Конфигурация gateway:
- Wi-Fi credentials
- MQTT server
- MQTT credentials
- topics
- LoRa pins
- queue sizes
- LED/button pins

---

## 9. Backend и MQTT-инфраструктура (для программирования hardware не нужен)

---

## 10. Database schema / storage (для программирования hardware не нужен)

---

## 11. IaC / localhosting (для программирования hardware не нужен)

---

## 12. Переход на STM32L431CCT6

### 12.1 Что выбрано
- **MCU:** STM32L431CCT6
- **IDE:** STM32CubeIDE
- **Config пинов:** CubeMX
- **HAL:** STM32Cube HAL
- **RTOS:** нет
- **подход:** bare-metal

### 12.2 Главные архитектурные принципы STM32-версии
- никаких блокировок в бизнес-логике;
- никакого `HAL_Delay()` в основном коде;
- без `malloc/free`;
- весь код в USER CODE блоках;
- код стараться писать вне `main.c`, выносить в отдельные файлы
- IRQ только для флагов;
- low-power first;
- датчики выключаются через MOSFET;
- wake-up от RTC и LoRa AUX;
- GPS, HW390, DS18B20, INA219 остаются логическими узлами проекта.
- LoRa E22 остаётся транспортом (протокол LoRa EBYTE E22 описан в приложенной библиотеке как reference);

Библиотека `EBYTE22.h/.cpp` важна как эталон протокола E22.
### Она показывает:
- режимы работы;
- работу с AUX;
- configuration frames;
- wireless config;
- crypt key;
- WOR cycle;
- radio address;
- packet lengths;
- power settings.


---

## 13. Уже сделанные настройки STM32 CubeMX

### 13.1 Общая конфигурация
- SYSCLK = MSI
- RTC = LSE
- LPUART1 clock = HSI
- проект рассчитан на low power

### 13.2 Назначенная периферия
| Периферия | Назначение | Ключевые настройки |
| :--- | :--- | :--- |
| **LPUART1** | Связь с LoRa EBYTE E22 | 9600 8N1, **Wake-Up из Stop mode** (по RXNE/Start bit), прерывания включены. |
| **USART1** | Чтение NMEA от GPS NEO-7M | 9600 8N1, включен **DMA RX (Circular)** для фонового приема данных, прерывания включены. |
| **USART3** | Отладка (Debug log) | 115200 8N1, используется для вывода логов на ПК через USB-UART. |
| **ADC1** | Чтение влажности почвы (HW390)| Канал IN6, 12-bit разрешение. |
| **I2C1** | Опрос INA219 (ток/напряжение) | Standard mode (100 kHz). |
| **RTC** | Часы реального времени | Включен, источник LSE. Планируется использование RTC Alarm для пробуждения. |
| **EXTI** | Прерывание от LoRa AUX | Линия EXTI7. |

### 13.3 Назначенные пины
| Пин | Функция в CubeMX | Метка (Label) | Направление | Описание (Подключение) |
| :--- | :--- | :--- | :--- | :--- |
| **PA0** | `GPIO_Output` | `LED_RED` | Output | Тестовый красный светодиод |
| **PA1** | `ADC1_IN6` | `HW390_ADC` | Analog | Аналоговый сигнал от датчика влажности |
| **PA2** | `LPUART1_TX` | `LORA_TX` | AF (TX) | Отправка команд в LoRa E22 |
| **PA3** | `LPUART1_RX` | `LORA_RX` | AF (RX) | Прием данных от LoRa E22 |
| **PA4** | `GPIO_Output` | `GPS_PWR` | Output | Управление MOSFET питания GPS |
| **PA5** | `GPIO_Output` | `HW390_PWR`| Output | Управление MOSFET питания датчика влажности |
| **PA6** | `GPIO_Output` | `DS18B20_DQ`| Output/Input| Линия данных OneWire (программный поллинг) |
| **PA7** | `GPIO_EXTI7` | `LORA_AUX` | EXTI | Пин состояния (Busy/Ready) от LoRa E22 |
| **PA8** | `GPIO_Output` | `DS_PWR` | Output | Управление MOSFET питания DS18B20 |
| **PA9** | `USART1_TX` | `GPS_TX` | AF (TX) | Отправка команд конфигурации в GPS |
| **PA10**| `USART1_RX` | `GPS_RX` | AF (RX) | Прием потока данных от GPS |
| **PA13**| `SYS_JTMS-SWDIO` | `SWDIO` | AF | Линия данных программатора ST-Link |
| **PA14**| `SYS_JTCK-SWCLK` | `SWCLK` | AF | Линия тактирования программатора ST-Link |
| **PB0** | `GPIO_Output` | `LORA_M0` | Output | Управление режимом работы E22 (Pin M0) |
| **PB1** | `GPIO_Output` | `LORA_M1` | Output | Управление режимом работы E22 (Pin M1) |
| **PB6** | `I2C1_SCL` | `INA219_SCL`| AF (SCL) | I2C тактирование монитора питания |
| **PB7** | `I2C1_SDA` | `INA219_SDA`| AF (SDA) | I2C данные монитора питания |
| **PB10**| `USART3_TX` | `DEBUG_TX` | AF (TX) | Вывод отладочной информации (логгер) |
| **PB11**| `USART3_RX` | `DEBUG_RX` | AF (RX) | Ввод отладочных команд (опционально) |
| **PC13**| `GPIO_Input` | `USER_BTN` | Input | Кнопка на плате (активный 0 / замыкает на землю) |
| **PC14**| `RCC_OSC32_IN` | `LSE_IN` | AF | Кварц 32.768 кГц |
| **PC15**| `RCC_OSC32_OUT`| `LSE_OUT` | AF | Кварц 32.768 кГц |


---

## 14. MOSFET / power gating

### Что уже решено
Питание датчиков будет отключаться через MOSFET.

### Кластеры питания
- `GPS_PWR`
- `HW390_PWR`
- `DS_PWR`

### Смысл
STM32 должна:
- включить датчик;
- подождать стабилизации;
- сделать измерение;
- выключить питание;
- уйти в sleep или stop mode.

---

## 15. GPS логика в STM32-версии

GPS u-blox NEO-7M остаётся отдельным узлом:
- UART: `USART1`
- питание: `GPS_PWR`
- будет использоваться для:
  - получения координат;
  - получения UTC;
  - синхронизации RTC;
  - периодического включения только на время измерения.

---

## 16. HW390 калибровка

Для STM32-версии будет важно иметь режим калибровки датчика влажности HW390:
1) измерить raw для сухого состояния и мокрого в течение 5-10 секунд (время должно быть настраиваемым параметром в конфиге);
2) сохранить эти значения как пороги 0% и 100% (где-нибудь в конфигурации, возможно отдельной);
3) пересчитывать ADC в проценты по калиброванной шкале.
*Важно*: В STM32 калибровочные значения должны храниться во flash

---

## 17. Что должен знать следующий ИИ

### Главные текущие факты
- sensor-node в актуальной прошивке — `main_sensor_wroom_2.py`
- gateway — `main_gateway.py`
- LoRa-пакеты строятся как: Смотрите пункт 3.10
- MQTT-топики: Смотрите пункт 4.5
- backend уже принимает эти данные
- STM32L431CCT6 уже выбран как целевая платформа для sensor-node
- CubeIDE/CubeMX — выбранная среда для STM32-этапа
- power gating через MOSFET — обязательная часть low-power архитектуры
- периферия и периферия STM32 уже назначены (смотрите разделы 13.2 и 13.3)
- Пока использовать статический NODE_ID. В текущей реализации на MicroPython NODE_ID также задаётся статически в конфигурации (hardcoded)

**Примечание о будущем:** Нижеперечисленное не реализовано, не планируется к реализации в текущей итерации, и НЕ должно влиять на архитектуру прошивки:
- шифрование LoRa, whitelist, auto-provisioning датчиков, fragmentation

## 18. Прилагаемые файлы:
### 18.1 Исходная hardware-ветка проекта
#### Sensor / LoRa 
- `main_sensor_wroom_2.py`
- `sensors.py`
- `lora_mini_lib.py`
- `utils.py`
- `config_common.py`
- `make_payload.py`
- `hw_calibration_idea.py` (идея для калибровки датчика влажности HW390)
#### Gateway (только для контекста, gateway останется на ESP32 MicroPython)
- `main_gateway.py`
- `config_gateway.py`

### 18.2 Файлы, связанные с библиотекой из интернета EBYTE22 и LoRa-протоколом
- `EBYTE22.h`
- `EBYTE22.cpp`

### 18.3 Текущие наработки STM32CubeIDE / CubeMX

#### Основные факты
- Проект создан в STM32CubeIDE, привязан к `.ioc` (файл `L431_Sensor_Node.ioc`).
- Используется стандартная структура CubeMX/CubeIDE.
- **Главное правило:** все изменения пинов, периферии и clock tree — только через CubeMX, генерация кода — тоже через CubeMX.
- Пользовательский код в автогенерируемых файлах (`main.c`, `gpio.c`, и т.д.) писать только внутри `/* USER CODE ... */` блоков.
- `Includes` в дереве проекта — это отображение путей поиска, **не место для своих `.h`**.

#### Куда класть свои файлы
- Свои `.h` → `Core/Inc/`
- Свои `.c` → `Core/Src/`

#### Рекомендуемая структура проекта (физическая)
ProjectRoot/
├── Core/
│   ├── Inc/                         ← сюда пользователь будет добавлять свои .h
│   │   ├── main.h, gpio.h, ...      ← автогенерируемые
│   │   └── sensor_node.h, lora_driver.h, gps_driver.h, hw390.h, ina219.h, power_ctrl.h
│   └── Src/                         ← сюда пользователь будет добавлять свои .c
│       ├── main.c, gpio.c, ...      ← автогенерируемые
│       └── sensor_node.c, lora_driver.c, gps_driver.c, hw390.c, ina219.c, power_ctrl.c
├── Drivers/                         ← HAL, CMSIS (не трогать)
├── Startup/                         ← startup assembly (не трогать)
├── L431_Sensor_Node.ioc             ← главный конфиг CubeMX
├── .mxproject
└── STM32L431CCTX_FLASH.ld           ← linker script
Было 
#### Пользовательские модули (предстоит добавить)
- `sensor_node.c/.h` — бизнес-логика сенсора
- `lora_driver.c/.h` — работа с LoRa E22
- `gps_driver.c/.h` — работа с GPS NEO-7M
- `hw390.c/.h` — датчик влажности
- `ina219.c/.h` — монитор питания
- `power_ctrl.c/.h` — управление MOSFET (питание датчиков)
- `payload_builder.c/.h` — формирование строк вида `device_id;timestamp;msg_rnd_id;msg_type;payload` 

#### Pinout и периферия
См. раздел **13.3** (таблица пинов) и **13.2** (периферия). Там зафиксированы все назначения.
