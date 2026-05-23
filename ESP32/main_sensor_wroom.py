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

# Импорты для датчиков
import onewire
import ds18x20
from machine import UART

# Наши импорты
import config_common as config
# import config_esp32_c3 as config
from lora_mini_lib import LoRaMiniLib
from utils import RingBuffer
from sensors import HW_Sensor, DS_Sensor, GPS_Sensor

# Задержка старта (подключиться к REPL через mpremote)
time.sleep(2)

DEFAULT_TIME = (2026, 3, 10,    1,    15, 30, 0,   0)
rtc = machine.RTC()
rtc.datetime(DEFAULT_TIME)

# Чтобы проверить время через стандартный модуль 'time'
print(f"time.localtime(): {time.localtime()}")

# Инициализация периферии
led = machine.Pin(config.PIN_LED, machine.Pin.OUT, value=0)  # выключен
button = machine.Pin(config.PIN_BUTTON, machine.Pin.IN, machine.Pin.PULL_UP)

# Инициализация LoRa (передаём весь модуль config — библиотека ожидает атрибуты LORA_*)
lora = LoRaMiniLib(config)
print("LoRa inited")

# Инициализация датчиков (используем config.PIN_DS и config.PIN_HW напрямую)
hw_sensor = HW_Sensor(config.PIN_HW, config.HW_WET_VALUE, config.HW_DRY_VALUE)
ds_sensor = DS_Sensor(config.PIN_DS)


gps_sensor = GPS_Sensor(config.GPS_UART_NUM, config.GPS_TX, config.GPS_RX, config.GPS_BAUD)
gps_sensor.power_on()
# я понимаю, что потом будет gps_sensor.power_off(), но ГЛОНАСС конфигураця загружается в долгую память геомодуля, так что нормально.
# То есть я хочу гарантировать подльзователю, что мы на ГЛОНАСС, и потом уже делать замеры с учётом cold start
gps_sensor.configure_glonass_only(save=True, cold_start=True)

# Функции чтения датчиков
def read_humidity():
    return hw_sensor.read_percent()

def read_temperature():
    return ds_sensor.read_temperature()

def read_gps_mock():
    lat = 54.842621 + random.uniform(-0.05, 0.05)
    lon = 83.087844 + random.uniform(-0.05, 0.05)
    return (lat, lon)

def get_iso_timestamp():
    """
    Возвращает время в формате ISO 8601: YYYY-MM-DDTHH:MM:SSZ
    """
    t = time.gmtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z".format(
        t[0], t[1], t[2], t[3], t[4], t[5]
    )

def make_humidity_payload(
    humidity: float,
    device_id: str = None,
    timestamp: str = None,
) -> str:
    if timestamp is None:
        timestamp = get_iso_timestamp()
    return f"{device_id},{timestamp},{humidity:.2f}"

def send_humidity(
        humidity: float,
        device_id = None,
        timestamp = None
    ):
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()

    msg_rnd_id = random.randint(0, 999_999)
    msg_type = "hum"

    payload = f"{device_id};{timestamp};{msg_rnd_id};{msg_type};{humidity:.2f}"
    lora.send_bytes(payload.encode("utf-8"))

def send_temperature(
        temperature: float,
        device_id = None,
        timestamp = None
    ):
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()

    msg_rnd_id = random.randint(0, 999_999)
    msg_type = "tmp"

    payload = f"{device_id};{timestamp};{msg_rnd_id};{msg_type};{temperature:.2f}"
    lora.send_bytes(payload.encode("utf-8"))

def send_gps(
        lat: float, # -90..+90
        lon: float, # -180..+180
        device_id = None,
        timestamp = None
    ):
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()
    
    msg_rnd_id = random.randint(0, 999_999)
    msg_type = "gps"

    payload = f"{device_id};{timestamp};{msg_rnd_id};{msg_type};{lat:.6f},{lon:.6f}"
    lora.send_bytes(payload.encode("utf-8"))

def send_state(
        rssi: int,
        snr: float,
        battery: float,
        online: bool = True,
        device_id = None,
        timestamp = None
    ):
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()

    msg_rnd_id = random.randint(0, 999_999)
    msg_type = "stt"

    payload = f"{device_id};{timestamp};{msg_rnd_id};{msg_type};{rssi},{snr:.2f},{battery:.1f},{online}"
    lora.send_bytes(payload.encode("utf-8"))

history = RingBuffer(max_size=config.HISTORY_SIZE)
seq_num = 0

def do_command(command, *params):
    if command == "SLEEP":
        print("sleep 3 secs")
        time.sleep(3)
    else:
        print(params)
    return  

def do_payload(payload, payload_bytes):
    global lora
    device_id, timestamp, msg_rnd_id, msg_type, msg, *other = payload.split(";")

    key = (device_id, timestamp, msg_rnd_id)
    
    if key in history:
        print("History has this")
        return
    
    history.add(key)
    
    # если нам
    if device_id == config.NODE_ID:
        if msg_type == "cmd":
            do_command(msg, *other)
    else:
        # Кричим дальше
        lora.send_bytes(payload_bytes)

# Основной цикл
clicked = False
cnt = 0
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

                do_payload(text, received)
        except Exception as e:
            print("LoRa receive error:", e)

    # TODO: замеры не по кнопке, а нормально через проверку пора ли
    # Как эта проверка выглядит: три отдельные функции, которые проверяют пора ли делать замер. Проверяют прошёл ли заданный в конфиге интервал времени с последнего замера, либо если установлен флаг NEED_READ_VALUE_DS/HW/GEO
    # if настало время сделать замер влаги:
    #     humid = read_humidity()
    #     print(f"  humid: {humid}")
    #     send_humidity(humid)
    #     time.sleep(1)
    # if настало время сделать замер температуры:
    #     temp = read_temperature()
    #     print(f"  temp: {temp}")
    #     send_temperature(temp)
    #     time.sleep(1)
    # if настало время сделать замер gps и сверить время со спутника:
    #     gps_sensor.power_on()

    # если нажали
    print("try click (1 sec)")
    led.value(1)
    for i in range(100):
        if not button.value():
            clicked = not clicked
            break
        time.sleep(0.01)
    led.value(0)
        
    print(clicked)
    if clicked:
        device_id = random.randint(0, 15)
        print(f" ID = {device_id}")

        # Читаем датчики
        humid = read_humidity()
        print(f"  humid: {humid}")
        send_humidity(humid, device_id=device_id)
        time.sleep(1)

        temp = read_temperature()
        print(f"  temp: {temp}")
        send_temperature(temp, device_id=device_id)
        time.sleep(1)

        # это тестовый код, поэтому только при нажатии кнопки норм
        gps_sensor.poll()
        if gps_sensor.check_ready():
            date, timee, lat, lon = gps_sensor.read_lat_lon()
            print(f"  gps: {date} {timee} {lat}, {lon}")
            send_gps(lat, lon, device_id=device_id)
            gps_sensor.power_off()
            time.sleep(1)
        else:
            lat = 54.847487 + random.uniform(-0.001, +0.001)
            lon = 83.092509 + random.uniform(-0.001, +0.001)
            send_gps(lat, lon, device_id=device_id)
            print(f"  gps: {lat}, {lon}")
            time.sleep(1)

        # TODO
        # rssi, snr = read_rssi_snr()
        # battery = read_battery()
        rssi = random.randint(-90, -30)  # Типичный диапазон RSSI: от -90 до -30 dBm
        snr = random.randint(0, 30)       # Типичный SNR: от 0 до 30 dB
        battery = random.randint(30, 100) # Заряд батареи: от 30% до 100%
        online = True
        send_state(rssi, snr, battery, online, device_id=device_id)
        print("  state: x x x online")
        time.sleep(1)


    # Небольшая пауза
    if cnt > 5:
        time.sleep(28)
    else:
        cnt += 1
    print("check 2 secs!")
    time.sleep(2)
