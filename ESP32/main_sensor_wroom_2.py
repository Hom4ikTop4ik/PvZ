# main_sensor.py
import esp32
import machine
import time
import random

import config_common as config
from lora_mini_lib import LoRaMiniLib
from utils import RingBuffer, LoRaTxQueue, lora_tx_pump
from sensors import HW_Sensor, DS_Sensor, GPS_Sensor, INA219,   HW_ERROR_SETTINGS, HW_ERROR_VALUE, DS_ERROR_VALUE, GPS_ERROR_VALUE

# Включатели телеметрии (если False — не добавляем в очередь вообще)
need_humidity_info = True
need_temperature_info = True
need_gps_info = True
need_status_info = True

# Одноразовые триггеры "сделай сейчас"
force_humidity_measure = False
force_temperature_measure = False
force_gps_measure = False
force_status_measure = False

_last_hw_ms = 0
_last_ds_ms = 0
_last_gps_ms = 0
_last_status_ms = 0

def _period_due(last_ms: int, period_s: int) -> bool:
    if not period_s or period_s <= 0:
        return False
    return time.ticks_diff(time.ticks_ms(), last_ms) >= (int(period_s * 1000))

def check_need_humidity_measurement() -> bool:
    if not need_humidity_info:
        return False
    if force_humidity_measure:
        return True
    return _period_due(_last_hw_ms, config.HW_PERIOD_MS)

def check_need_temperature_measurement() -> bool:
    if not need_temperature_info:
        return False
    if force_temperature_measure:
        return True
    return _period_due(_last_ds_ms, config.DS_PERIOD_MS)

def check_need_gps_measurement() -> bool:
    if not need_gps_info:
        return False
    if force_gps_measure:
        return True
    return _period_due(_last_gps_ms, config.GPS_PERIOD_MS)

def check_need_status_measurement() -> bool:
    if not need_status_info:
        return False
    if force_status_measure:
        return True
    return _period_due(_last_status_ms, config.STATUS_PERIOD_MS)

def check_smth():
    return check_need_humidity_measurement() or check_need_temperature_measurement() or check_need_gps_measurement() or check_need_status_measurement()




def set_rtc_utc(date_ymd: str, time_hms: str) -> bool:
    try:
        year = int(date_ymd[0:4])
        month = int(date_ymd[5:7])
        day = int(date_ymd[8:10])
        hour = int(time_hms[0:2])
        minute = int(time_hms[3:5])
        second = int(time_hms[6:8])
        machine.RTC().datetime((year, month, day, 0, hour, minute, second, 0))
        return True
    except Exception as e:
        print("RTC set error:", e)
        return False


def wait_utc_date_time(gps : GPS_Sensor, timeout_s: int = 60, log_every_ms: int = 2000):
    """
    Крутит poll() пока не появятся utc_date_ymd и utc_time_hms.
    Координаты/fix НЕ требуются.

    Возвращает: (date_ymd, time_hms) или (None, None) при timeout.
    """
    t_end = time.ticks_add(time.ticks_ms(), int(timeout_s) * 1000)
    last_log = time.ticks_ms()

    while time.ticks_diff(t_end, time.ticks_ms()) > 0:
        gps.poll()

        date_ymd = gps.utc_date_ymd
        time_hms = gps.utc_time_hms
        if date_ymd is not None and time_hms is not None:
            return date_ymd, time_hms

        if time.ticks_diff(time.ticks_ms(), last_log) > log_every_ms:
            last_log = time.ticks_ms()
            print("GPS time sync:", "fix=", gps.fix, "date=", gps.utc_date_ymd, "time=", gps.utc_time_hms)

        time.sleep_ms(50)

    return None, None

# Вспомогательное
def get_iso_timestamp():
    t = time.gmtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z".format(t[0], t[1], t[2], t[3], t[4], t[5])

def _enqueue_payload(payload: str) -> bool:
    """
    Добавляет сообщение в TX очередь.
    Возвращает False если очередь заполнена/сообщение слишком длинное.
    """
    return tx_queue.add(payload.encode("utf-8"))




# Периферия
time.sleep(2)

DEFAULT_TIME = (2026, 3, 19,    3,    7, 32, 0,   0)
rtc = machine.RTC()
rtc.datetime(DEFAULT_TIME)

# Чтобы проверить время через стандартный модуль 'time'
print(f"time.localtime(): {time.localtime()}")


led = machine.Pin(config.PIN_LED, machine.Pin.OUT, value=0)
button = machine.Pin(config.PIN_BUTTON, machine.Pin.IN, machine.Pin.PULL_UP)

# LoRa
lora = LoRaMiniLib(config)
# esp32.wake_on_ext0(pin=lora.aux, level=1)
print("LoRa inited")


history = RingBuffer(max_size=config.HISTORY_SIZE)

# TX queue: ограничиваем RAM
tx_queue = LoRaTxQueue(
    max_items=config.LORA_TX_QUEUE_SIZE,
    max_msg_len=config.LORA_TX_MAX_LEN,
)

# Датчики
hw_sensor = HW_Sensor(config.PIN_HW, config.HW_WET_VALUE, config.HW_DRY_VALUE)
ds_sensor = DS_Sensor(config.PIN_DS)

gps_sensor = GPS_Sensor(config.GPS_UART_NUM, config.GPS_TX, config.GPS_RX, config.GPS_BAUD)
gps_sensor.power_on()

i2c = machine.I2C(0, scl=machine.Pin(config.INA_SCL), sda=machine.Pin(config.INA_SDA), freq=100000)
print("I2C scan:", [hex(addr) for addr in i2c.scan()])
ina219 = INA219(i2c, vmin=config.INA219_MIN, vmax=config.INA219_MAX)

print("Waiting UTC date/time from GPS (no fix needed)...")
date_ymd, time_hms = wait_utc_date_time(gps_sensor, timeout_s=int(getattr(config, "GPS_TIME_SYNC_TIMEOUT_S", 5)))

if date_ymd is None or time_hms is None:
    print("GPS TIME SYNC TIMEOUT")
else:
    led.value(1)
    print("GPS UTC:", date_ymd, time_hms)
    ok = set_rtc_utc(date_ymd, time_hms)
    print("RTC set:", ok, "now:", get_iso_timestamp())

# SEND_* теперь КЛАДУТ в очередь (не отправляют сразу)
def send_humidity(humidity: float, device_id=None, timestamp=None) -> bool:
    if not need_humidity_info:
        return False
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"{device_id};{timestamp};{msg_rnd_id};hum;{humidity:.2f}"
    return _enqueue_payload(payload)

def send_temperature(temperature: float, device_id=None, timestamp=None) -> bool:
    if not need_temperature_info:
        return False
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"{device_id};{timestamp};{msg_rnd_id};tmp;{temperature:.2f}"
    return _enqueue_payload(payload)

def send_gps(lat: float, lon: float, device_id=None, timestamp=None) -> bool:
    if not need_gps_info:
        return False
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"{device_id};{timestamp};{msg_rnd_id};gps;{lat:.6f},{lon:.6f}"
    return _enqueue_payload(payload)

def send_state(rssi: int, snr: float, battery: float, online: bool = True, device_id=None, timestamp=None) -> bool:
    if not need_status_info:
        return False
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"{device_id};{timestamp};{msg_rnd_id};stt;{rssi},{snr:.2f},{battery:.1f},{online}"
    return _enqueue_payload(payload)

# Команды / ретрансляция
def do_command(command, *params):
    global force_humidity_measure, force_temperature_measure, force_gps_measure, force_status_measure
    # сюда вы потом добавите команды типа HUM_ON/HUM_OFF и т.п.
    if command == "SLEEP":
        print("sleep 3 secs")
        time.sleep(3)
        return
    if command == "FORCE_HUM":
        force_humidity_measure = True
        return
    if command == "FORCE_TMP":
        force_temperature_measure = True
        return
    if command == "FORCE_GEO":
        force_gps_measure = True
        return
    if command == "FORCE_STT":
        force_status_measure = True
        return

def do_payload(payload: str, payload_bytes: bytes):
    device_id, timestamp, msg_rnd_id, msg_type, msg, *other = payload.split(";")
    key = (device_id, timestamp, msg_rnd_id)

    if key in history:
        return
    history.add(key)

    if device_id == config.NODE_ID:
        if msg_type == "cmd":
            do_command(msg, *other)
    else:
        # ретрансляция — тоже можно через очередь (чтобы не мешать TX pacing)
        # Если хотите оставить "как сейчас" (мгновенно) — замените на lora.send_bytes(payload_bytes)
        print("                     echo")
        tx_queue.add(payload_bytes)

# Основной цикл: быстрый, без sleep(1) между отправками
while True:
    # тест: кнопка форсит замеры
    if not button.value():
        force_humidity_measure = True
        force_temperature_measure = True
        force_gps_measure = True
        force_status_measure = True

    # 1) RX LoRa
    try:
        received = lora.receive_bytes()
        if received:
            try:
                text = received.decode("utf-8", "ignore")
            except Exception:
                text = repr(received)
            do_payload(text, received)
    except Exception as e:
        print("LoRa receive error:", e)

    # 2) GPS poll
    gps_sensor.poll()

    if check_smth():
        # device_id = random.randint(0, 15)
        device_id = config.NODE_ID
        print(f" ID = {device_id}")

    # 3) Планировщик замеров -> данные сразу кладём в очередь
    if check_need_humidity_measurement():
        humid = hw_sensor.read_percent()
        if not humid:
            humid = HW_ERROR_SETTINGS

        if send_humidity(humid, device_id=device_id):
            print(f"  humid: {humid}")
            _last_hw_ms = time.ticks_ms()
            force_humidity_measure = False

    if check_need_temperature_measurement():
        temp = ds_sensor.read_temperature()
        if not temp:
            temp = DS_ERROR_VALUE

        if send_temperature(temp, device_id=device_id):
            print(f"  temp: {temp}")
            _last_ds_ms = time.ticks_ms()
            force_temperature_measure = False

    if check_need_gps_measurement():
        # если нет fix — не сбрасываем force, просто ждём следующей итерации
        if gps_sensor.check_ready():
            lat, lon = gps_sensor.read_lat_lon()
            if send_gps(lat, lon, device_id=device_id):
                print(f"  gps: {lat}, {lon}")
                _last_gps_ms = time.ticks_ms()
                force_gps_measure = False
        else:
            # заглушка Пятёрка
            # lat = 54.847487 + random.uniform(-0.001, +0.001)
            # lon = 83.092509 + random.uniform(-0.001, +0.001)
            # заглушка Экспо
            # lat = 55.007487 + random.uniform(-0.001, +0.001)
            # lon = 82.742509 + random.uniform(-0.001, +0.001)
            # lat = GPS_ERROR_VALUE
            # lon = GPS_ERROR_VALUE
            lat = 54.843696 + random.uniform(-0.00001, +0.00001)
            lon = 83.091125 + random.uniform(-0.00001, +0.00001)
            if send_gps(lat, lon, device_id=device_id):
                print(f"  gps_mock: {lat}, {lon}")
                _last_gps_ms = time.ticks_ms()
                force_gps_measure = False


    if check_need_status_measurement():
        # TODO: подставите реальные rssi/snr/battery
        rssi = random.randint(-90, -30)  # Типичный диапазон RSSI: от -90 до -30 dBm
        snr = random.randint(0, 30)       # Типичный SNR: от 0 до 30 dB
        # battery = random.randint(30, 100) # Заряд батареи: от 30% до 100%
        battery = ina219.read_percent()
        current_tok = ina219.get_current_ma()

        if not battery:
            battery = 52.42
        if not current_tok:
            current_tok = 0.0
            
        print(f"  INA {battery} {current_tok}")
        online = "online"
        
        if send_state(rssi, snr, battery, online, device_id=device_id):
            print(f"  state: x x {battery} online  {current_tok}")
            _last_status_ms = time.ticks_ms()
            force_status_measure = False

    # 4) TX pump: отправляем не чаще, чем раз в N мс
    result, msg = lora_tx_pump(lora, tx_queue, config.LORA_TX_MIN_INTERVAL_MS)
    if result == "sent":
        print(f"sent: {msg}")

    # маленькая пауза, чтобы не крутиться в 100% CPU
    time.sleep_ms(20)
