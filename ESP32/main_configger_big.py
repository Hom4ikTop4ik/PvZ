# main_configger.py
import machine
import time
import sys
import config_common as config

# Константы команд (для E22 400T30D)
CMD_SAVE = 0xC0  # Сохранить параметры
CMD_READ = 0xC1  # Прочитать параметры
CMD_SET = 0xC0   # Установить параметры (то же что и SAVE)

# Значения по умолчанию (можно менять)
DEFAULT_CONFIG = {
    'address_high': 0x00,      # Старший байт адреса
    'address_low': 0x00,       # Младший байт адреса
    'uart_baudrate': 0b011,    # 9600 (000=1200, 001=2400, 010=4800, 011=9600, 100=19200...)
    'uart_parity': 0b00,        # 0=8N1 (8 бит, нет четности, 1 стоп)
    'air_baudrate': 0b100,      # 2.4kbps (000=0.3k, 001=1.2k, 010=2.4k, 011=4.8k, 100=9.6k...)
    'channel': 0x17,            # Канал (410M + CH*1M) 0x17=433M
    'tx_power': 0b00,           # 0=22dBm, 1=17dBm, 2=13dBm, 3=10dBm
    'rssi_ambient': 0b0,        # Включить RSSI
    'transmission_mode': 0b0,   # 0=fixed, 1=transparent
    'io_drive': 0b0,            # 0=push-pull, 1=open-drain
    'wake_up_time': 0b00,       # 0=250ms, 1=500ms, 2=750ms, 3=1000ms
    'fec': 0b1,                 # 0=disable, 1=enable
    'power_save': 0b00           # 0=normal, 1=wake-up, 2=power-saving
}

def enter_config_mode():
    """Вход в режим конфигурации (M0=1, M1=1)"""
    m0 = machine.Pin(config.LORA_M0, machine.Pin.OUT)
    m1 = machine.Pin(config.LORA_M1, machine.Pin.OUT)
    
    m0.value(0)
    m1.value(1)
    time.sleep(0.1)
    
    # Инициализация UART на фиксированной скорости для конфига (9600)
    uart = machine.UART(
        config.LORA_UART_NUM,
        baudrate=9600,  # В режиме конфига всегда 9600
        tx=machine.Pin(config.LORA_TX),
        rx=machine.Pin(config.LORA_RX),
        timeout=100
    )
    
    return uart

def exit_config_mode():
    """Выход из режима конфигурации (M0=0, M1=0)"""
    m0 = machine.Pin(config.LORA_M0, machine.Pin.OUT)
    m1 = machine.Pin(config.LORA_M1, machine.Pin.OUT)
    
    m0.value(0)
    m1.value(0)
    time.sleep(0.1)

def read_config():
    """Чтение текущей конфигурации модуля"""
    uart = enter_config_mode()
    
    # Команда чтения
    uart.write(bytes([CMD_READ]))
    time.sleep(0.3)
    
    # Чтение ответа (6 байт)
    if uart.any():
        data = uart.read(6)
        print(len(data))
        if data:
            print("\nТекущая конфигурация:")
            print(f"  ADDH: 0x{data[0]:02X}")
            print(f"  ADDL: 0x{data[1]:02X}")
            print(f"  SPED: 0x{data[2]:02X} (биты: 7-5=скорость UART, 4-3=четность, 2-0=скорость эфира)")
            print(f"  CHAN: 0x{data[3]:02X} (канал)")
            print(f"  OPTION: 0x{data[4]:02X} (биты: 7-6=мощность, 5=RSSI, 4-3=режим, 2=драйвер, 1-0=пробуждение)")
            return data
    
    print("Ошибка чтения конфигурации")
    return None

def write_config(config_dict):
    """Запись конфигурации в модуль"""
    uart = enter_config_mode()
    
    # Формируем 6 байт конфигурации
    # REG0: ADDH
    # REG1: ADDL
    # REG2: SPED (скорости и четность)
    sped = (config_dict['uart_baudrate'] << 5) | \
           (config_dict['uart_parity'] << 3) | \
           (config_dict['air_baudrate'] << 0)
    
    # REG3: CHAN (канал)
    chan = config_dict['channel']
    
    # REG4: OPTION (разные настройки)
    option = (config_dict['tx_power'] << 6) | \
             (config_dict['rssi_ambient'] << 5) | \
             (config_dict['transmission_mode'] << 3) | \
             (config_dict['io_drive'] << 2) | \
             (config_dict['wake_up_time'] << 0)
    
    # REG5: (запасной/не используется в E22?)
    
    config_bytes = bytes([
        config_dict['address_high'],
        config_dict['address_low'],
        sped,
        chan,
        option,
        0x00  # REG5
    ])
    
    print(f"\nУстанавливаю конфигурацию: {config_bytes.hex()}")
    
    # Отправка команды установки
    uart.write(bytes([CMD_SET]) + config_bytes)
    time.sleep(0.2)
    
    # Проверка ответа
    if uart.any():
        response = uart.read()
        print(f"Ответ: {response}")
        if response and len(response) >= 1 and response[0] == CMD_SET:
            print("✓ Конфигурация применена")
            return True
    
    print("✗ Ошибка применения конфигурации")
    return False

def print_config_summary():
    """Вывод описания устанавливаемых параметров"""
    print("\n" + "="*50)
    print("УСТАНАВЛИВАЕМЫЕ ПАРАМЕТРЫ LoRa МОДУЛЯ")
    print("="*50)
    print("• Адрес: 0x0000 (широковещательный)")
    print("• UART: 9600 8N1 (скорость обмена с ESP)")
    print("• Эфир: 2.4 kbps (скорость передачи в радио)")
    print("• Канал: 0x17 (433 МГц)")
    print("• Мощность: 22 dBm (максимальная)")
    print("• RSSI: Включен")
    print("• Режим: Transparent (прозрачный)")
    print("• FEC: Включен (помехоустойчивость)")
    print("• Режим питания: Normal")
    print("="*50)

def main():
    print("LoRa Configurator для E22 400T30D")
    print("1 - Прочитать текущую конфигурацию")
    print("2 - Установить конфигурацию по умолчанию")
    print("3 - Выход")
    
    choice = input("Выберите действие: ").strip()
    
    if choice == '1':
        exit_config_mode()  # гарантируем нормальный режим перед входом
        time.sleep(0.1)
        read_config()
        
    elif choice == '2':
        print_config_summary()
        confirm = input("\nУстановить эти параметры? (y/n): ").strip().lower()
        if confirm == 'y':
            if write_config(DEFAULT_CONFIG):
                print("\n✓ Модуль настроен!")
            else:
                print("\n✗ Ошибка настройки!")
        else:
            print("Отменено")
    
    # Выход в нормальный режим
    exit_config_mode()
    print("\nМодуль переведен в нормальный режим")

if __name__ == "__main__":
    time.sleep(1)
    main()
