# main_configger_small.py
import machine
import time
import config_common as config

print("Настройка LoRa модуля...")

# Пины
m0 = machine.Pin(config.LORA_M0, machine.Pin.OUT)
m1 = machine.Pin(config.LORA_M1, machine.Pin.OUT)
aux = machine.Pin(config.LORA_AUX, machine.Pin.IN)

# Вход в режим конфигурации (M0=1, M1=1)
print("Вход в режим конфигурации...")
m0.value(0)
m1.value(1)
time.sleep_ms(500)  # Увеличил задержку

# Ждем готовности модуля
print("Ждем AUX...")
for i in range(50):  # 500ms таймаут
    if aux.value() == 1:
        print("AUX OK")
        break
    time.sleep_ms(10)

# UART на 9600
print("Инициализация UART...")
uart = machine.UART(
    config.LORA_UART_NUM,
    baudrate=9600,
    tx=machine.Pin(config.LORA_TX),
    rx=machine.Pin(config.LORA_RX),
    timeout=1000
)
time.sleep_ms(200)  # Даем UART стабилизироваться

# Очищаем буфер
while uart.any():
    uart.read()

# Команда чтения конфигурации
print("Отправка команды чтения (0xC1)...")
uart.write(b'\xc1\x00\x06')
time.sleep_ms(500)  # Ждем ответ

print("Проверка ответа...")
if uart.any():
    data = uart.read()
    print(f"Получено {len(data)} байт: {data.hex()}")

time.sleep_ms(500)  # Ждем 


# Команда записи конфигурации (вернётся 9 байт ответа с конфигом!!!)
print("Отправка команды записи (0xC0)...")
base_lora_config = b'\x00\x00\x00\x62\x00\x17'
uart.write(b'\xc0\x00\x06' + base_lora_config)
time.sleep_ms(500)  # Ждем ответ (вся конфигурация)

print("Проверка ответа...")
if uart.any():
    data = uart.read()
    print(f"Получено {len(data)} байт: {data.hex()}")

else:
    print("Нет ответа от модуля!")

# Выход из режима конфигурации
print("Выход из режима конфигурации...")
m0.value(0)
m1.value(0)
time.sleep_ms(200)

print("Готово!")