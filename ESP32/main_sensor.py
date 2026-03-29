# main_sensor.py
# Чтение датчиков DS18B20 и DHT11, прослушка LoRa, отправка показаний по LoRa.
# GPS — заглушка с методами:
# - start_search()    — запускает поиск (печать)
# - check_ready()     — проверяет готовность (20% шанс установить ready=True)
# - read_position()   — возвращает (lat, lon) если ready, иначе (None, None)
#
# Использует config_common как config (везде напрямую config.PIN_DS, config.PIN_HW и т.д.).

import machine
import time
import random

import config_common as config
from lora_mini_lib import LoRaMiniLib

# Задержка старта
time.sleep(1)

# Импорты для датчиков
import onewire
import ds18x20

# Инициализация LoRa (передаём весь модуль config — библиотека ожидает атрибуты LORA_*)
try:
    lora = LoRaMiniLib(config)
except Exception as e:
    print("LoRa init error:")
    lora = None
    raise

# GPS-заглушка
class GPSModuleStub:
    def __init__(self):
        self.ready = False
        self.lat = None
        self.lon = None
        self.searching = False

    def start_search(self):
        # Команда модулю: начать поиск позиции (заглушка)
        self.searching = True
        self.ready = False
        self.lat = None
        self.lon = None
        print("GPS: start_search() called")

    def check_ready(self):
        # Опросить модуль — с шансом 20% считать, что позиция готова
        print("GPS: check_ready() called")
        if not self.searching:
            return False
        if random.random() < 0.2:
            base_lat = 54.84
            base_lon = 83.08
            self.lat = base_lat + (random.random() - 0.5) * 0.01
            self.lon = base_lon + (random.random() - 0.5) * 0.01
            self.ready = True
            self.searching = False
            print("GPS: position acquired (stub)")
            return True
        return False

    def read_position(self):
        # Возвращает (lat, lon) если готово, иначе (None, None)
        if self.ready and self.lat is not None and self.lon is not None:
            print("GPS: read_position() ->", (self.lat, self.lon))
            return (self.lat, self.lon)
        print("GPS: read_position() -> not ready")
        return (None, None)

# Инициализация датчиков (используем config.PIN_DS и config.PIN_HW напрямую)
hw = machine.ADC(machine.Pin(config.PIN_HW, machine.Pin.IN))
hw.atten(machine.ADC.ATTN_11DB)

try:
    ds_pin = machine.Pin(config.PIN_DS, machine.Pin.IN)
    ow = onewire.OneWire(ds_pin)
    ds = ds18x20.DS18X20(ow)
    roms = ds.scan()
    if not roms:
        print("No DS18B20 sensor found")
except Exception as e:
    print("DS18B20 init error:", e)
    roms = []

temp = None
humidity = None

# Инициализация GPS-заглушки
gps = GPSModuleStub()

# Функции чтения датчиков
def read_temperature():
    global temp
    try:
        if roms:
            ds.convert_temp()
            time.sleep_ms(750)
            temp = ds.read_temp(roms[0])
        else:
            temp = None
    except Exception as e:
        print("DS read error:", e)
        temp = None
    return temp

def read_humidity():
    if not hw: return -14.0
    samples = [ hw.read() for _ in range(5) ]
    print(samples)
    raw = sum(samples) // len(samples)
    clamped_raw = max(min(raw, config.HW_DRY_VALUE), config.HW_WET_VALUE)
    span = config.HW_DRY_VALUE - config.HW_WET_VALUE
    if span == 0: return 0.0
    moisture = 100 * (1 - (clamped_raw - config.HW_WET_VALUE) / span)
    return round(moisture, 1)


# Вспомогательная функция форматирования полей для payload
def fmt(v, fmtstr="{:.6f}"):
    if v is None:
        return ""
    try:
        return fmtstr.format(v)
    except Exception:
        return str(v)

# Запустим первоначальный поиск GPS
gps.start_search()

# Основной цикл
while True:
    
    # Прослушка LoRa-эфира (если модуль инициализирован)
    if lora:
        try:
            received = lora.receive_bytes()
            if received:
                try:
                    text = received.decode("utf-8", "ignore")
                except Exception:
                    text = repr(received)
                print(f"[LoRa RX {len(received)}]: {text}")
        except Exception as e:
            print("LoRa receive error:", e)

    # Читаем датчики
    t = read_temperature()
    h = read_humidity()

    # Периодически проверяем GPS: сначала check_ready, затем при готовности read_position
    gps_ready_now = gps.check_ready()
    if gps_ready_now:
        lat, lon = gps.read_position()
    else:
        lat, lon = (None, None)


    # Отправляем данные по LoRa (если имеются хоть какие-то показания)
    if lora and (t is not None or h is not None or lat is not None or lon is not None):
        parts = [str(config.NODE_ID)]
        parts.append(fmt(t, "{:.2f}"))
        parts.append(fmt(h, "{:.2f}"))
        parts.append(fmt(lat, "{:.6f}"))
        parts.append(fmt(lon, "{:.6f}"))
        payload = ";".join(parts)
        try:
            lora.send_bytes(payload.encode("utf-8"))
            print("Sent via LoRa:", payload)
        except Exception as e:
            print("LoRa send error:", e)

        # Сбросим флаг GPS после чтения (для заглушки)
        if lat is not None or lon is not None:
            gps.ready = False
            gps.lat = None
            gps.lon = None
            gps.start_search()

    # Небольшая пауза
    time.sleep(1)
