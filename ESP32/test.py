# # # test_lora_config.py
# # import machine
# # import time
# # import config_common as config

# # print("Тест LoRa модуля...")

# # # Пины
# # m0 = machine.Pin(config.LORA_M0, machine.Pin.OUT)
# # m1 = machine.Pin(config.LORA_M1, machine.Pin.OUT)
# # aux = machine.Pin(config.LORA_AUX, machine.Pin.IN)

# # print(f"AUX начальное состояние: {aux.value()}")

# # # Режим конфигурации (M0=1, M1=1)
# # print("Вход в режим конфигурации...")
# # m0.value(1)
# # m1.value(1)

# # # Ждем AUX (модуль должен переключиться)
# # print("Ждем AUX=1...")
# # for i in range(100):  # 1 секунда
# #     if aux.value() == 1:
# #         print(f"AUX=1 через {i*10}мс")
# #         break
# #     time.sleep_ms(10)
# # else:
# #     print("AUX не поднялся!")

# # # UART
# # uart = machine.UART(
# #     config.LORA_UART_NUM,
# #     baudrate=9600,
# #     tx=machine.Pin(config.LORA_TX),
# #     rx=machine.Pin(config.LORA_RX),
# #     timeout=1000
# # )
# # time.sleep_ms(200)

# # # Очищаем буфер
# # while uart.any():
# #     uart.read()

# # # Пробуем разные команды
# # commands = [b'\xC1', b'\xC0', b'\x00', b'\xFF']

# # for cmd in commands:
# #     print(f"\nОтправка команды: {cmd.hex()}")
# #     uart.write(cmd)
# #     time.sleep_ms(500)
    
# #     if uart.any():
# #         resp = uart.read()
# #         print(f"Ответ: {resp.hex()}")
# #     else:
# #         print("Нет ответа")

# # # Выход
# # m0.value(0)
# # m1.value(0)
# # print("\nТест завершен")






# # config_like_coolterm.py
# import machine
# import time
# import config_common as config

# print("Конфигуратор как в Coolterm...")

# # Пины
# m0 = machine.Pin(config.LORA_M0, machine.Pin.OUT)
# m1 = machine.Pin(config.LORA_M1, machine.Pin.OUT)

# # Режим конфигурации
# m0.value(0)
# m1.value(1)
# time.sleep_ms(1000)  # Большая задержка как в Coolterm

# # UART как в терминале
# uart = machine.UART(
#     config.LORA_UART_NUM,
#     baudrate=9600,
#     tx=machine.Pin(config.LORA_TX),
#     rx=machine.Pin(config.LORA_RX),
#     timeout=2000,      # Большой таймаут
#     timeout_char=100,  # Таймаут между символами
#     rxbuf=1024         # Большой буфер
# )
# time.sleep_ms(1000)    # Ждем как в терминале

# # Отправляем команду как в Coolterm
# cmd = bytes([0xC1, 0x00, 0x06])
# print(f"Отправка: {cmd.hex()}")

# # Отправляем байт за байтом как терминал
# uart.write(cmd)

# time.sleep_ms(500)  # Ждем ответ

# # Читаем все доступные байты
# resp = b''
# for _ in range(10):  # Максимум 10 байт
#     if uart.any():
#         byte = uart.read()
#         if byte:
#             resp += byte
#             print(f"Получен байт: {byte.hex()}")
#             time.sleep_ms(10)
#     else:
#         break

# print(f"Полный ответ: {resp.hex()}")

# # Возврат
# m0.value(0)
# m1.value(0)
# print("Готово!")
# pins_high.py


import machine
import time

print("Поднимаем все пины в HIGH")
print("Переключайте светодиод между пинами и GND")

# Все доступные пины
pins_to_set = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 18, 19, 20, 21]

for pin_num in pins_to_set:
    try:
        pin = machine.Pin(pin_num, machine.Pin.OUT)
        pin.value(1)  # HIGH
        print(f"GPIO{pin_num} = HIGH")
    except:
        print(f"GPIO{pin_num} = ERROR")

print("\nВсе пины подняты в 1")
print("Подключайте светодиод (с резистором) к любому пину и GND")
print("Светодиод должен гореть")

# Бесконечный цикл, чтоб программа не завершалась
while True:
    time.sleep(1)