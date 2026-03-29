# lora_mini_lib.py
import machine
import time
import random

# TODO
# from constants import LoRaConst

class LoRaMiniLib:
    # Константы
    MAX_PACKET_SIZE = 240   # Максимальный размер пакета в байтах
    ACK_TIMEOUT = 20 *1000  # Таймаут ожидания ACK в мс
    AUX_WAIT_TIMEOUT = 100  # Таймаут готовности по сигналу от AUX в мс
    UART_WAIT_TIMEOUT = 50
    
    def __init__(self, config):
        """
        Инициализация LoRa модуля.
        config должен содержать:
        - LORA_M0, LORA_M1, LORA_TX, LORA_RX
        - LORA_UART_NUM, LORA_BAUDRATE
        """

        self._last_rssi_dbm = None
        self._last_snr_db = None
        self._last_rx_ms = None
        
        self._append_rssi = bool(getattr(config, "LORA_APPEND_RSSI", True))
        
        # Инициализация пинов режима
        self.m0 = machine.Pin(config.LORA_M0, machine.Pin.OUT)
        self.m1 = machine.Pin(config.LORA_M1, machine.Pin.OUT)
        self.aux = machine.Pin(config.LORA_AUX, machine.Pin.IN)
        
        # Установка нормального режима (0, 0)
        self.set_mode_normal()
        
        # Инициализация UART
        self.uart = machine.UART(
            config.LORA_UART_NUM,
            baudrate=config.LORA_BAUDRATE,
            tx=machine.Pin(config.LORA_TX),
            rx=machine.Pin(config.LORA_RX)
        )

        # TODO: добавить заливку конфига по умолчанию в LoRa-модуль (радиоканал, id, режимы и прочие настройки)
    
    def set_mode_normal(self):
        """Установка нормального режима (0, 0)"""
        self.m0.value(0)
        self.m1.value(0)
        time.sleep_ms(50)
    
    def set_mode_wor_rx(self):
        """
        Установка WOR режима (M0=1, M1=0)
        """
        self.m0.value(1)
        self.m1.value(0)
        time.sleep_ms(50)

    def wait_for_aux(self, timeout_ms=AUX_WAIT_TIMEOUT):
        """
        Ожидание, когда AUX перейдет в HIGH (модуль готов)
        
        Args:
            timeout: максимальное время ожидания в мс
            
        Returns:
            bool: True если модуль готов, False если таймаут
        """
        start = time.ticks_ms()
        
        while time.ticks_diff(time.ticks_ms(), start) < timeout_ms:
            if self.aux.value() == 1:  # AUX HIGH = модуль готов
                return True
            time.sleep_ms(1)
        
        return False

    def send_bytes(self, data):
        """Отправляет байты (макс 240)"""
        if len(data) > self.MAX_PACKET_SIZE:
            return 1
        
        if not self.wait_for_aux():
            return 1
        
        try:
            if self.uart.write(data) == len(data):
                return 0
            return -1
        except:
            return 1

    def receive_bytes(self, timeout_ms=UART_WAIT_TIMEOUT):
        """Принимает байты (макс 240)"""
        # Ждем данные от модуля
        if not self.wait_for_aux():
            print("no aux")
            return None
        
        start = time.ticks_ms()
        buffer = b''
        
        if self.uart.any():
            buffer = self.uart.read()

        # while time.ticks_diff(time.ticks_ms(), start) < timeout_ms:
        #     if self.uart.any():
        #         chunk = self.uart.read()
        #         if chunk:
        #             buffer += chunk
        #             start = time.ticks_ms()
        #     time.sleep_ms(1)
        
        return buffer if buffer else None
    
    # UPDATED V2
    def receive_bytes_wor(self, timeout_ms=UART_WAIT_TIMEOUT):
        """
        Принимает байты (не более 240 обычно).
        В нормальном режиме можно ждать AUX.
        В WOR лучше не ждать AUX, а просто читать UART если есть.
        """
        # Если UART уже имеет данные — читаем сразу
        data = self.read_if_any()
        if data:
            return data

        # Если данных нет, в normal режиме можно подождать AUX и ещё раз попробовать
        # (в WOR это не обязательно, но не ломает)
        if not self.wait_for_aux(timeout_ms=self.AUX_WAIT_TIMEOUT):
            return None

        return self.read_if_any()
    
    def read_if_any(self):
        """
        Неблокирующее чтение UART.
        Важно для WOR: не ждём AUX вообще, просто читаем то что уже в UART.
        """
        try:
            if self.uart.any():
                return self.uart.read()
        except Exception as e:
            print("read_if_any error:", e)
        return None
