# main_test_gps.py
# Минимальный тест: получить UTC дату/время от GPS при первой возможности,
# выставить RTC на ESP32, затем (опционально) продолжать ждать координаты и отправить GEO по LoRa.
#
# Важно:
# - Для DATE/TIME координаты (fix) не нужны: RMC может дать дату/время даже при fix=0.
# - Для GEO (lat/lon) нужен fix и открытое небо.

import time
import random
import machine

import config_common as config
from lora_mini_lib import LoRaMiniLib
from sensors import GPS_Sensor  # или GPSSensor — подставьте как у вас в sensors.py


def get_iso_timestamp():
    t = time.gmtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z".format(t[0], t[1], t[2], t[3], t[4], t[5])


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


def wait_utc_date_time(gps, timeout_s: int = 60, log_every_ms: int = 2000):
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


def send_gps(lora, lat: float, lon: float, device_id=None, timestamp=None):
    if not device_id:
        device_id = config.NODE_ID
    if not timestamp:
        timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"{device_id};{timestamp};{msg_rnd_id};gps;{lat:.6f},{lon:.6f}"
    lora.send_bytes(payload.encode("utf-8"))
    print("LoRa TX:", payload)


print("Boot time (before GPS sync):", get_iso_timestamp())

# LoRa (можно выключить, если хотите только синхронизацию времени)
lora = LoRaMiniLib(config)
print("LoRa inited")

print("TIME now:", get_iso_timestamp())

# GPS
gps = GPS_Sensor(config.GPS_UART_NUM, config.GPS_TX, config.GPS_RX, getattr(config, "GPS_BAUD", 9600))

print("Waiting UTC date/time from GPS (no fix needed)...")
date_ymd, time_hms = wait_utc_date_time(gps, timeout_s=int(getattr(config, "GPS_TIME_SYNC_TIMEOUT_S", 60)))

if date_ymd is None or time_hms is None:
    print("GPS TIME SYNC TIMEOUT")
else:
    print("GPS UTC:", date_ymd, time_hms)
    ok = set_rtc_utc(date_ymd, time_hms)
    print("RTC set:", ok, "now:", get_iso_timestamp())

    # Опционально: дальше можете ждать fix и отправить GEO для проверки брокера
    # (если вы сейчас далеко от компьютера и хотите убедиться, что всё ок)
    while True:
        gps.poll()
        if gps.check_ready():
            d, t, lat, lon = gps.read_lat_lon()
            print("GPS FIX:", d, t, lat, lon)
            send_gps(lora, lat, lon)
        else:
            print("NaH")
        time.sleep_ms(100)
    else:
        print("GPS FIX TIMEOUT (date/time were still synced)")
