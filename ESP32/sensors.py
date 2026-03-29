# gps_sensor.py
# Класс-обёртка для GPS (u-blox NEO-7M/совместимые), чтение NMEA по UART + опциональная настройка GNSS через UBX.
# Подходит для MicroPython (machine.UART).
#
# Использование:
#   import config_common as config
#   from gps_sensor import GPSSensor
#   gps = GPSSensor(uart_num=config.GPS_UART_NUM, tx=config.GPS_TX, rx=config.GPS_RX, baudrate=9600)
#   gps.configure_glonass_only(save=True, cold_start=True)   # опционально
#   while True:
#       gps.poll()
#       lat, lon = gps.read_position()
#       ...

import machine
import struct
import time

import onewire
import ds18x20


HW_ERROR_VALUE  = -500 # 500 — ошибка "сервера", в нашем случае У-датчика
HW_ERROR_SETTINGS = -400 # 400 — ошибка клиента, то есть пользователя кто конфиг настроил
DS_ERROR_VALUE  = -500
GPS_ERROR_VALUE = -500

# DS18B20 (onewire + ds18x20) в виде класса для MicroPython
class DS_Sensor:
    def __init__(self, pin: int, read_delay_ms: int = 750):
        self.pin_num = pin
        self.read_delay_ms = read_delay_ms

        self._ds = None
        self._roms = []

        try:
            ds_pin = machine.Pin(self.pin_num, machine.Pin.IN)
            ow = onewire.OneWire(ds_pin)
            self._ds = ds18x20.DS18X20(ow)
            self._roms = self._ds.scan() or []
            if not self._roms:
                print("No DS18B20 sensor found")
        except Exception as e:
            print("DS18B20 init error:", e)
            self._ds = None
            self._roms = []

    def available(self) -> bool:
        return self._ds is not None and bool(self._roms)

    def read_temperature(self, index: int = 0):
        """
        Возвращает температуру float (°C) или None.
        """
        if not self.available():
            return None

        try:
            self._ds.convert_temp()
            time.sleep_ms(self.read_delay_ms)
            return self._ds.read_temp(self._roms[index])
        except Exception as e:
            print("DS read error:", e)
            return None


class HW_Sensor:
    def __init__(
        self,
        pin: int,
        wet_value: int,
        dry_value: int,
        samples: int = 5,
        atten=machine.ADC.ATTN_11DB,
    ):
        self.pin_num = pin
        self.wet_value = wet_value
        self.dry_value = dry_value
        self.samples = samples

        self._adc = None
        try:
            self._adc = machine.ADC(machine.Pin(self.pin_num, machine.Pin.IN))
            self._adc.atten(atten)
        except Exception as e:
            print("HW sensor init error:", e)
            self._adc = None

    def available(self) -> bool:
        return self._adc is not None

    def read_raw(self):
        if not self.available():
            return None
        try:
            vals = [self._adc.read() for _ in range(self.samples)]
            return sum(vals) // len(vals)
        except Exception as e:
            print("HW read error:", e)
            return None

    def read_percent(self):
        raw = self.read_raw()
        if raw is None:
            return None
        
        if self.dry_value < self.wet_value:
            return None

        clamped_raw = max(min(raw, self.dry_value), self.wet_value)
        span = self.dry_value - self.wet_value
        
        # UPDATED V4
        # спрятано чтобы пользователь понял: откалибровано неверно!
        # if span <= 0:
            # return 0.0

        moisture = 100 * (1 - (clamped_raw - self.wet_value) / span)
        return round(moisture, 1)


class INA219:
    REG_CONFIG = 0x00
    REG_SHUNT_VOLTAGE = 0x01
    REG_BUS_VOLTAGE = 0x02
    REG_POWER = 0x03
    REG_CURRENT = 0x04
    REG_CALIBRATION = 0x05

    def __init__(self, i2c, address=0x40, vmin: float = 5.4, vmax: float = 8.4):
        self.i2c = i2c
        self.address = address

        self.vmin = float(vmin)
        self.vmax = float(vmax)

        self.current_lsb = 0.0001   # 100 uA/bit
        self.power_lsb = 0.002      # 2 mW/bit
        self.calibration_value = 4096

        self._ok = False
        try:
            # Быстрая проверка присутствия на I2C (скан может быть дорогим, но надёжный)
            # Если хотите без scan, можно просто попробовать read_register(REG_CONFIG)
            if hasattr(self.i2c, "scan"):
                devs = self.i2c.scan()
                if self.address not in devs:
                    print("INA219 not found on I2C addr:", hex(self.address))
                    self._ok = False
                    return

            self.configure()
            # Доп. подтверждение, что устройст��о реально отвечает
            _ = self.read_register(self.REG_CONFIG)
            self._ok = True
        except Exception as e:
            print("INA219 init/config error:", e)
            self._ok = False

    def available(self) -> bool:
        return self._ok

    def _fail(self, msg: str, e: Exception | None = None):
        # при ошибках I2C считаем датчик недоступным, чтобы не спамить исключениями
        if e is not None:
            print(msg, e)
        else:
            print(msg)
        self._ok = False

    def write_register(self, reg, value) -> bool:
        if not self.available():
            return False
        try:
            data = struct.pack(">H", value)
            self.i2c.writeto_mem(self.address, reg, data)
            return True
        except Exception as e:
            self._fail("INA219 write error:", e)
            return False

    def read_register(self, reg):
        if not self.available():
            return None
        try:
            data = self.i2c.readfrom_mem(self.address, reg, 2)
            return struct.unpack(">H", data)[0]
        except Exception as e:
            self._fail("INA219 read error:", e)
            return None

    def read_signed(self, reg):
        value = self.read_register(reg)
        if value is None:
            return None
        if value > 32767:
            value -= 65536
        return value

    def configure(self):
        if not self.write_register(self.REG_CALIBRATION, self.calibration_value):
            return False
        # 32V range, 320mV shunt range, 12-bit ADC, continuous
        config = 0x399F
        return self.write_register(self.REG_CONFIG, config)

    def get_shunt_voltage_mv(self):
        raw = self.read_signed(self.REG_SHUNT_VOLTAGE)
        if raw is None:
            return None
        return raw * 0.01

    def get_bus_voltage_v(self):
        raw = self.read_register(self.REG_BUS_VOLTAGE)
        if raw is None:
            return None
        return ((raw >> 3) * 4) / 1000.0

    def get_current_ma(self):
        if not self.available():
            return None
        # некоторые INA219 требуют заново писать калибровку перед чтением CURRENT/POWER
        if not self.write_register(self.REG_CALIBRATION, self.calibration_value):
            return None
        raw = self.read_signed(self.REG_CURRENT)
        if raw is None:
            return None
        return raw * self.current_lsb * 1000.0

    def get_power_w(self):
        if not self.available():
            return None
        if not self.write_register(self.REG_CALIBRATION, self.calibration_value):
            return None
        raw = self.read_register(self.REG_POWER)
        if raw is None:
            return None
        return raw * self.power_lsb

    def get_load_voltage_v(self):
        vbus = self.get_bus_voltage_v()
        vsh = self.get_shunt_voltage_mv()
        if vbus is None or vsh is None:
            return None
        return vbus + (vsh / 1000.0)

    def read_battery_voltage_v(self, use_load_voltage: bool = True):
        if not self.available():
            return None
        try:
            return self.get_load_voltage_v() if use_load_voltage else self.get_bus_voltage_v()
        except Exception as e:
            self._fail("INA219 read voltage error:", e)
            return None

    def read_percent(self, use_load_voltage: bool = True):
        v = self.read_battery_voltage_v(use_load_voltage=use_load_voltage)
        if v is None:
            return None
        if self.vmax <= self.vmin:
            return None
        # if v <= self.vmin:
        #     return 0.0
        # if v >= self.vmax:
        #     return 100.0
        return float((v - self.vmin) / (self.vmax - self.vmin) * 100.0)



class GPS_Sensor:
    def __init__(
        self,
        uart_num: int,
        tx: int,
        rx: int,
        baudrate: int = 9600,
        pwr_pin: int | None = None,
        pwr_on_level: int = 1,
        start_delay_ms: int = 300,
    ):
        self.uart = machine.UART(uart_num, baudrate=baudrate, tx=tx, rx=rx)

        self._pwr = None
        self._pwr_on_level = pwr_on_level
        if pwr_pin is not None:
            self._pwr = machine.Pin(pwr_pin, machine.Pin.OUT)
            self.power_off()

        self.start_delay_ms = int(start_delay_ms)
        self._started_at_ms = None

        # Latest parsed values (UTC)
        self.latitude = None
        self.longitude = None
        self.fix = 0  # from GGA quality (0 = invalid)

        # Date/time from RMC preferred, time from GGA possible
        self.utc_time_hms = None  # "HH:MM:SS"
        self.utc_date_ymd = None  # "YYYY-MM-DD"

        self.satellites = None
        self.hdop = None
        self.altitude = None

        self._last_nmea = None
        self._last_update_ms = time.ticks_ms()

        self._line_buffer = ""  # Буфер для неполных строк

    # -------- Power control --------
    def power_on(self):
        if self._pwr is not None:
            self._pwr.value(self._pwr_on_level)
        self._started_at_ms = time.ticks_ms()
        self.reset()
        self.flush_uart()

    def power_off(self):
        if self._pwr is not None:
            self._pwr.value(0 if self._pwr_on_level else 1)
        self._started_at_ms = None

    def powered(self) -> bool:
        if self._pwr is None:
            return True
        return self._pwr.value() == self._pwr_on_level

    def check_started(self) -> bool:
        """
        True если прошло start_delay_ms после power_on().
        Это НЕ означает наличие fix — только что модуль "ожил".
        """
        if self._started_at_ms is None:
            return False
        return time.ticks_diff(time.ticks_ms(), self._started_at_ms) >= self.start_delay_ms

    def flush_uart(self):
        try:
            while self.uart.any():
                self.uart.read()
        except Exception:
            pass

    def reset(self):
        self.latitude = None
        self.longitude = None
        self.fix = 0
        self.utc_time_hms = None
        self.utc_date_ymd = None
        self.satellites = None
        self.hdop = None
        self.altitude = None
        self._last_nmea = None
        self._last_update_ms = time.ticks_ms()

    # -------- Public API --------
    def poll(self, max_bytes: int = 512):
        """
        Неблокирующий опрос UART: читает кусок буфера и парсит строки NMEA.
        Можно вызывать хоть каждую итерацию main loop.
        """
        if not self.uart.any():
            return

        data = self.uart.read(max_bytes)
        if not data:
            return

        try:
            text = data.decode("utf-8", "replace")
            # print(f"GPS text: {text}")
        except Exception:
            return
        
        # UPDATED V3: 
        # Добавляем новые данные в буфер
        self._line_buffer += text
        # Разбиваем буфер на строки
        lines = self._line_buffer.split("\n")
        # Последний элемент может быть неполным - оставляем в буфере
        # если же было полное, то последний элеемнт будет равен "" (пустой строке)
        self._line_buffer = lines[-1]

        for raw in lines[:-1]:
            # print(raw)
            line = raw.strip()
            if not line:
                continue
            if line.endswith("\r"):
                line = line[:-1]

            # обычно $GPGGA/$GPRMC или $GNGGA/$GNRMC
            if line.startswith("$GP") or line.startswith("$GN"):
                self._handle_nmea_line(line)

    def check_ready(self) -> bool:
        """
        Готово для установки времени и получения координат:
        - fix >= 1
        - lat/lon есть
        - есть UTC дата и время (предпочтительно из RMC)
        """
        if not (self.fix is not None and self.fix >= 1):
            print(" no fix")
            # return False
        if self.latitude is None or self.longitude is None:
            return False
        # if self.utc_date_ymd is None or self.utc_time_hms is None:
        #     return False
        return True

    # def read_date_time(self):
    #     """
    #     Возвращает (date_ymd, time_hms) или (None, None)
    #     date_ymd: "YYYY-MM-DD"
    #     time_hms: "HH:MM:SS"
    #     """
    #     if not self.check_ready():
    #         return None, None
    #     return self.utc_date_ymd, self.utc_time_hms
    
    def read_lat_lon(self):
        """
        Возвращает (lat, lon) или (None, None)
        """
        if not self.check_ready():
            return None, None
        return self.latitude, self.longitude

    def last_nmea(self):
        return self._last_nmea

    def age_ms(self) -> int:
        return time.ticks_diff(time.ticks_ms(), self._last_update_ms)

    # -------- NMEA parsing helpers --------
    @staticmethod
    def _nmea_coord_to_float(coord: str, direction: str):
        # NMEA: DDMM.MMMM (lat), DDDMM.MMMM (lon)
        if not coord or not direction:
            return None
        if "." not in coord:
            return None
        if len(coord) < 6:
            return None

        # до точки > 4 цифр => долгота (DDDMM)
        if len(coord.split(".")[0]) > 4:
            deg = int(coord[:3])
            minute = float(coord[3:])
        else:
            deg = int(coord[:2])
            minute = float(coord[2:])

        val = deg + minute / 60.0
        if direction in ("S", "W"):
            val *= -1
        return val

    @staticmethod
    def _parse_nmea_time_hms(t: str):
        # 114831.00 -> 11:48:31
        if not t or len(t) < 6:
            return None
        hh = t[0:2]
        mm = t[2:4]
        ss = t[4:6]
        return f"{hh}:{mm}:{ss}"

    @staticmethod
    def _parse_nmea_date_ymd(d: str):
        # 151225 -> 2025-12-15
        if not d or len(d) != 6:
            return None
        day = int(d[0:2])
        month = int(d[2:4])
        year = 2000 + int(d[4:6])
        return f"{year:04d}-{month:02d}-{day:02d}"

    def _handle_nmea_line(self, line: str):
        self._last_nmea = line
        parts = line.split(",")

        # --- GGA (time + fix quality + sats + hdop + altitude + lat/lon) ---
        if line.startswith("$GPGGA") or line.startswith("$GNGGA"):
            # time
            if len(parts) > 1 and parts[1]:
                t = self._parse_nmea_time_hms(parts[1])
                if t:
                    # время можно взять и из GGA (даты тут нет)
                    if self.utc_time_hms is None:
                        self.utc_time_hms = t

            # lat/lon
            if len(parts) > 5 and parts[2] and parts[3] and parts[4] and parts[5]:
                lat = self._nmea_coord_to_float(parts[2], parts[3])
                lon = self._nmea_coord_to_float(parts[4], parts[5])
                if lat is not None and lon is not None:
                    self.latitude = lat
                    self.longitude = lon

            # fix quality
            if len(parts) > 6 and parts[6]:
                try:
                    self.fix = int(parts[6])
                except ValueError:
                    pass

            # satellites
            if len(parts) > 7 and parts[7]:
                try:
                    self.satellites = int(parts[7])
                except ValueError:
                    pass

            # hdop
            if len(parts) > 8 and parts[8]:
                try:
                    self.hdop = float(parts[8])
                except ValueError:
                    pass

            # altitude
            if len(parts) > 9 and parts[9]:
                try:
                    self.altitude = float(parts[9])
                except ValueError:
                    pass

            self._last_update_ms = time.ticks_ms()
            return

        # --- RMC (date + time + status + lat/lon) ---
        if line.startswith("$GPRMC") or line.startswith("$GNRMC"):
            # parts: [0]$GPRMC [1]time [2]status(A/V) [3]lat [4]N/S [5]lon [6]E/W ... [9]date
            # time
            if len(parts) > 1 and parts[1]:
                t = self._parse_nmea_time_hms(parts[1])
                if t:
                    self.utc_time_hms = t

            # status: A=valid, V=void (можно использовать как доп. признак)
            # но fix всё равно лучше брать из GGA quality
            # status = parts[2] if len(parts) > 2 else ""

            # lat/lon
            if len(parts) > 6 and parts[3] and parts[4] and parts[5] and parts[6]:
                lat = self._nmea_coord_to_float(parts[3], parts[4])
                lon = self._nmea_coord_to_float(parts[5], parts[6])
                if lat is not None and lon is not None:
                    self.latitude = lat
                    self.longitude = lon

            # date
            if len(parts) > 9 and parts[9]:
                d = self._parse_nmea_date_ymd(parts[9])
                if d:
                    self.utc_date_ymd = d

            self._last_update_ms = time.ticks_ms()
            return









    # UPDATED V2
    def configure_glonass_only(self, save: bool = True, cold_start: bool = True) -> bool:
        """
        Настройка u-blox (NEO-7M/совместимые) через UBX:
          - QZSS OFF
          - SBAS OFF
          - GPS OFF
          - GLONASS ON

        Важно:
        - Метод блокирующий (использует короткие sleep) и рассчитан на вызов при старте/настройке.
        - Для отправки UBX использует self.uart (machine.UART).
        """

        def calculate_checksum(msg: bytes) -> bytes:
            ck_a = 0
            ck_b = 0
            for b in msg:
                ck_a = (ck_a + b) & 0xFF
                ck_b = (ck_b + ck_a) & 0xFF
            return bytes([ck_a, ck_b])

        def send_ubx(msg_class: int, msg_id: int, payload: bytes):
            header = b"\xB5\x62"
            length = struct.pack("<H", len(payload))
            body = bytes([msg_class, msg_id]) + length + payload
            checksum = calculate_checksum(body)
            packet = header + body + checksum
            self.uart.write(packet)

        # Пакет CFG-GNSS (0x06 0x3E)
        payload_cfg_gnss = (
            b"\x00"      # msgVer
            b"\x20"      # numTrkChHw (32)
            b"\x20"      # numTrkChUse (32)
            b"\x04"      # numConfigBlocks (GPS, SBAS, QZSS, GLONASS)

            # GPS (gnssId=0) disable
            b"\x00" b"\x04" b"\xFF" b"\x00" b"\x00\x00\x00\x00"

            # SBAS (gnssId=1) disable
            b"\x01" b"\x00" b"\x03" b"\x00" b"\x00\x00\x00\x00"

            # QZSS (gnssId=5) disable
            b"\x05" b"\x00" b"\x03" b"\x00" b"\x00\x00\x00\x00"

            # GLONASS (gnssId=6) enable
            b"\x06" b"\x08" b"\xFF" b"\x00" b"\x01\x00\x00\x00"
        )

        try:
            # 1) Применяем настройки GNSS
            send_ubx(0x06, 0x3E, payload_cfg_gnss)  # CFG-GNSS
            time.sleep_ms(300)

            # 2) Сохраняем в память (CFG-CFG)
            if save:
                # ваш payload из примера (best-effort)
                save_payload = b"\x00\x00\x00\x00\x1F\x00\x00\x00\x00\x00\x00\x00\x1F"
                send_ubx(0x06, 0x09, save_payload)  # CFG-CFG
                time.sleep_ms(300)

            # 3) Cold Start reset (CFG-RST), чтобы изменения применились
            if cold_start:
                rst_payload = b"\xFF\xFF\x01\x00"
                send_ubx(0x06, 0x04, rst_payload)  # CFG-RST
                time.sleep_ms(300)

            return True
        except Exception as e:
            print("configure_glonass_only error:", e)
            return False
