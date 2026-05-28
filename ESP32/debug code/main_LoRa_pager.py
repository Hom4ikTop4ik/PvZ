from machine import Pin, UART
import time



time.sleep(0.5)
def pin_test():
    print("Pin states:")
    pins = [0, 1, 2, 3, 4, 20, 21]
    for pin in pins:
        p = Pin(pin, Pin.IN)
        print(f"GPIO{pin}: {p.value()}")

pin_test()




print("APPLE")
# Константы подключения
BUTTON_PIN = 3
LED_PIN = 4

# LoRa модуль EBYTE 400T30D
LORA_UART_PORT = 0
LORA_TX_PIN = 1      # для ESP это RX
LORA_RX_PIN = 2      # для ESP это TX
LORA_AUX_PIN = 0      # Пин AUX (подключить к GPIO)
#LORA_M0_PIN = 1       # Пин M0 (если используется)
#LORA_M1_PIN = 2       # Пин M1 (если используется)
LORA_BAUDRATE = 9600

# Инициализация пинов
button = Pin(BUTTON_PIN, Pin.IN, Pin.PULL_UP)
led = Pin(LED_PIN, Pin.OUT)
lora_aux = Pin(LORA_AUX_PIN, Pin.IN)

# Опционально - если конфигурируете модуль через M0, M1
# lora_m0 = Pin(LORA_M0_PIN, Pin.OUT)
# lora_m1 = Pin(LORA_M1_PIN, Pin.OUT)

# UART для LoRa
lora = UART(LORA_UART_PORT, baudrate=LORA_BAUDRATE, tx=LORA_TX_PIN, rx=LORA_RX_PIN)





def wait_for_aux_high():
    """Ожидание когда AUX станет высоким (модуль готов)"""
    while lora_aux.value() == 0:
        time.sleep(0.01)
    print("LoRa ready")

def set_lora_normal_mode():
    """Установка LoRa в нормальный режим (M0=0, M1=0)"""
    # lora_m0.value(0)
    # lora_m1.value(0)
    wait_for_aux_high()
    print("LoRa in normal mode")

def send_notification():
    """Отправка уведомления с проверкой готовности через AUX"""
    if lora_aux.value() == 1:  # Модуль готов к передаче
        try:
            lora.write(b'PING')
            print("Notification sent!")
            return True
        except:
            print("Send error")
            return False
    else:
        print("LoRa not ready")
        return False

def check_messages():
    """Проверка входящих сообщений"""
    if lora.any():
        try:
            msg = lora.read()
            if msg == b'PING':
                print("Notification received!")
                blink_led(3)
                return True
        except:
            print("Receive error")
    return False

def blink_led(times):
    """Мигание светодиодом"""
    for i in range(times):
        led.value(1)
        time.sleep(0.1)
        led.value(0)
        time.sleep(0.1)





def main():
    last_button_state = True
    
    # Инициализация LoRa
    set_lora_normal_mode()
    
    print("LoRa Pager Ready!")
    print("Press button to notify other pager")
    print(f"AUX pin state: {lora_aux.value()}")
    
    while True:
        # Проверка кнопки
        button_state = button.value()
        if button_state == 0 and last_button_state == 1:
            if send_notification():
                blink_led(1)
        last_button_state = button_state
        
        # Проверка сообщений
        check_messages()
        
        time.sleep(0.05)

if __name__ == "__main__":
    main()