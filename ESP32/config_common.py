# config_common.py
# Конфигурация для ESP-32U + LoRa E22

# --- ИДЕНТИФИКАЦИЯ МОДУЛЯ ---
NODE_ID = 456



# --- LORA EBYTE E22 (UART) ---
# Доступные GPIO на ESP32-C3 super mini:
# 0,1,2,3,4,5,6,7,8,9,10,18,19,20,21

LORA_M0  = 5  # любой GPIO (5 или 21)
LORA_M1  = 18  # любой GPIO  
LORA_AUX = 19  # любой GPIO (лучше с прерыванием)

# UART пины (аппаратные):
# UART0: TX=21, RX=20 - но они уже заняты USB
# UART1: TX=6, RX=7  - СВОБОДНЫ!
LORA_TX  = 17   # ESP32 TX (подключать к RX LoRa)
LORA_RX  = 16   # ESP32 RX (подключать к TX LoRa)

# # Настройки эфира
LORA_UART_NUM = 1      # Используем аппаратный UART0
LORA_BAUDRATE = 9600   # Скорость общения с модулем



# --- ПЕРИФЕРИЯ ---
PIN_BUTTON = 0 # boot кнопка (нажато=0, отпущено=1)
PIN_LED = 2    # встроенный светодиод (0=светит, 1=тухнет)




# Дополнительная конфигурация для датчиков и GPS
PIN_DS = 32  # Пин для DS18B20 (температура), можно изменить
PIN_HW = 33  # Пин для HW-390 (влажность), можно изменить

GPS_UART_NUM = 2  # UART для GPS (отличный от LoRa UART)
GPS_TX = 26       # Втыкать в RX у GPS
GPS_RX = 25       # Втыкать в TX у GPS
GPS_BAUD = 9600   # Скорость UART для GPS


INA_SCL = 14
INA_SDA = 27


# --- дефолтные значения (на случай если калибровки ещё нет) ---
HW_WET_VALUE = 1200
HW_DRY_VALUE = 3500

# --- пробуем применить калибровку/оверрайды из config_hw.py ---
try:
    import config_hw  # файл создаётся/редактируется на FS
    if hasattr(config_hw, "HW_WET_VALUE"):
        HW_WET_VALUE = int(config_hw.HW_WET_VALUE)
    if hasattr(config_hw, "HW_DRY_VALUE"):
        HW_DRY_VALUE = int(config_hw.HW_DRY_VALUE)
except ImportError:
    pass

# 2 аккума 18650 Li-ion
INA219_MIN = 2.7 * 2 
INA219_MAX = 4.2 * 2 


HISTORY_SIZE = 200 # история последних сообщений


LORA_TX_QUEUE_SIZE = 24
LORA_TX_MAX_LEN = 240
LORA_TX_MIN_INTERVAL_MS = 1200
LORA_APPEND_RSSI = True

HW_PERIOD_MS = 60
DS_PERIOD_MS = 60
GPS_PERIOD_MS = 60
STATUS_PERIOD_MS = 60


