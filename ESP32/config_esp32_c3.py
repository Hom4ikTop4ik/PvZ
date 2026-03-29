# config_common.py
# Конфигурация для ESP-32U + LoRa E22

# --- ИДЕНТИФИКАЦИЯ МОДУЛЯ ---
NODE_ID = 1



# --- LORA EBYTE E22 (UART) ---
# Доступные GPIO на ESP32-C3 super mini:
# 0,1,2,3,4,5,6,7,8,9,10,18,19,20,21

LORA_M0  = 5  # любой GPIO
LORA_M1  = 20  # любой GPIO  
LORA_AUX = 21  # любой GPIO (лучше с прерыванием)

# UART пины (аппаратные):
# UART0: TX=21, RX=20 - но они уже заняты USB
# UART1: TX=6, RX=7  - СВОБОДНЫ!
LORA_TX  = 6   # ESP32 TX (подключать к RX LoRa)
LORA_RX  = 7   # ESP32 RX (подключать к TX LoRa)

# # Настройки эфира
LORA_UART_NUM = 1      # Используем аппаратный UART0
LORA_BAUDRATE = 9600   # Скорость общения с модулем



# --- ПЕРИФЕРИЯ ---
PIN_BUTTON = 9 # boot кнопка (нажато=0, отпущено=1)
PIN_LED = 8    # встроенный светодиод (0=светит, 1=тухнет)




# Дополнительная конфигурация для датчиков и GPS
PIN_DS = 10  # Пин для DS18B20 (температура), можно изменить
PIN_HW = 0   # Пин для HW-390 (влажность), можно изменить

GPS_UART_NUM = 2  # UART для GPS (отличный от LoRa UART)
GPS_TX = 3        # Втыкать в RX у GPS
GPS_RX = 4        # Втыкать в TX у GPS
GPS_BAUD = 9600   # Скорость UART для GPS


HW_DRY_VALUE = 3500
HW_WET_VALUE = 1200
