# config_gateway.py

# --- Настройки Wi-Fi ---
# WIFI_SSID = "HUAWEI-T1CNQ1" # пример сети
# WIFI_PASS = "Wifi_password" # пример пароля
# WIFI_SSID = "КаНура" # пример сети
# WIFI_PASS = "diy@or@die" # пример пароля
# WIFI_SSID = "Xiaomi 12 Lite" # пример сети
# WIFI_PASS = "naagliv324klovNit7io9" # пример пароля
WIFI_SSID = "NO_SSID"
WIFI_PASS = "NO_PASSWORD"

# --- Настройки MQTT Брокера ---
# ВАЖНО: Убедитесь, что IP 192.168.1.55 действительно принадлежит вашему ПК.
# (в Windows команда ipconfig, в Linux/Mac - ifconfig)


# "127.0.0.1:5173"
# "192.168.1.55:1883"  
# "5.129.250.254:1883"
MQTT_SERVER = "5.129.250.254"
MQTT_PORT = 1883
MQTT_USER = None
MQTT_PASS = None
MQTT_CLIENT_ID = "ESP32-C3_GW_TEST_0"

# --- Настройки проекта ---
ENV = "dev"
TENANT = "fake"

# 1. Топик для ПОДПИСКИ (Чтение команд)
# Здесь '+' НУЖЕН, чтобы слышать команды для любого устройства
TOPIC_SUB_COMMANDS = f"{ENV}/{TENANT}/devices/+/command"

# 2. Префикс для ПУБЛИКАЦИИ (Отправка данных)
# Здесь '+' НЕЛЬЗЯ. Мы будем дописывать /{device_id}/type в коде main.py
TOPIC_PUB_PREFIX = f"{ENV}/{TENANT}/sensors"



HISTORY_SIZE = 200 # история последних сообщений


LORA_TX_QUEUE_SIZE = 120 # кол-во сообщений
LORA_TX_MAX_LEN = 240 # длина одного сообщения
LORA_TX_MIN_INTERVAL_MS = 1200 # минимальная задержка (мс) между сообщениями





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

# --- Config portal ---
# После reset gateway ждет это время, чтобы пользователь успел зажать BOOT.
CONFIG_PORTAL_BOOT_WINDOW_MS = 10000
CONFIG_PORTAL_BOOT_HOLD_MS = 1000


