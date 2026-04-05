# Small Instructions для проекта PvZ
## Проект
IoT-система сбора телеметрии с датчиков → LoRa → Gateway → MQTT → Backend.

## Текущая задача
Перенос sensor-node с ESP32/MicroPython на **STM32L431CCT6** (bare-metal, STM32CubeIDE, HAL).

## Целевая архитектура STM32
- Без RTOS, без malloc/free, без блокирующих задержек в бизнес-логике
- IRQ → только установка флагов
- Power gating: MOSFET на GPS_PWR, HW390_PWR, DS_PWR
- Wake-up: RTC Alarm + LoRa AUX (EXTI)
- Низкое энергопотребление — приоритет

## Периферия (CubeMX, уже настроено)
| Периферия | Назначение | Ключевое |
|-----------|------------|----------|
| LPUART1 | LoRa E22 | Wake-up из Stop, 9600 8N1 |
| USART1 | GPS NEO-7M | DMA RX (circular), 9600 8N1 |
| USART3 | Debug log | 115200 8N1 |
| ADC1 IN6 | HW390 (влажность) | 12-bit |
| I2C1 | INA219 (ток/напряжение) | 100 kHz |
| RTC LSE | Время, Alarm для wake-up | |
| EXTI7 | LoRa AUX (прерывание) | |

## Пины (ключевые)
- `LORA_M0 PB0`, `LORA_M1 PB1`, `LORA_AUX PA7 (EXTI)`
- `GPS_PWR PA4`, `HW390_PWR PA5`, `DS_PWR PA8`
- `DS18B20_DQ PA6` (software OneWire)
- `USER_BTN PC13` (active low)

## Формат LoRa-пакета (текст)
```
device_id;timestamp;msg_rnd_id;msg_type;payload
```

**msg_type:** `hum`, `tmp`, `geo`, `stt`, `cmd`

Примеры payload (через `;`):
- humidity: `device_id,timestamp,humidity,seq`
- temperature: `device_id,timestamp,temperature,seq`
- location: `device_id,timestamp,lat,lon,seq`
- state: `device_id,timestamp,rssi,snr,battery,online,seq`
- ACK: `command_id,timestamp,status,details`

## Логика работы (перенос с main_sensor_wroom_2.py)
- Флаги потоков: `need_humidity_info`, `need_temperature_info`, `need_gps_info`, `need_status_info`
- Force-флаги: `force_humidity_measure`, `force_temperature_measure`, `force_geo_measure`, `force_status_measure`
- Таймеры последнего измерения: `_last_hw_ms`, `_last_ds_ms`, `_last_gps_ms`, `_last_status_ms`
- Отправка через очередь (`LoRaTxQueue`), pump с минимальным интервалом
- История сообщений (`RingBuffer`) для предотвращения broadcast-шторма
- Команды: `SLEEP`, `FORCE_HUM`, `FORCE_TMP`, `FORCE_GEO`, `FORCE_STT`

## Файловая структура проекта (Core/)
**Inc/**
- `sensor_node.h`, `lora_driver.h`, `gps_driver.h`, `hw390.h`, `ina219.h`, `power_ctrl.h`, `payload_builder.h`
**Src/**
- `sensor_node.c`, `lora_driver.c`, `gps_driver.c`, `hw390.c`, `ina219.c`, `power_ctrl.c`, `payload_builder.c`

## Требования к коду STM32
- Пишем в `/* USER CODE ... */` блоках
- Никаких `HAL_Delay()` в основном цикле
- LoRa-драйвер — по мотивам `EBYTE22.h/.cpp` (передача, режимы, AUX-ожидание)
- HW390: калибровка сухой/мокрой почвы → хранение во flash
- GPS: `configure_glonass_only()`, получение UTC и координат
- INA219: напряжение батареи, процент заряда
- RTC синхронизация от GPS UTC

## Чего НЕ делаем (не реализуем сейчас)
Шифрование LoRa, whitelist, auto-provisioning, fragmentation

## Gateway (остаётся на ESP32 MicroPython)
- `main_gateway.py`
- MQTT-топики: `dev/fake/sensors/{device_id}/humidity`, `/temperature`, `/location`, `/state`
- Подписка на команды: `dev/fake/devices/+/command`
