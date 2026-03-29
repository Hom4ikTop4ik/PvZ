from machine import ADC, Pin
import time

def test_adc_pins():
    """Тестирование доступных ADC пинов на ESP32-C3"""
    
    # ADC пины для ESP32-C3
    adc_pins = [0, 1, 2, 3, 4]
    
    for pin_num in adc_pins:
        try:
            # print(f"Testing ADC pin {pin_num}...")
            adc = ADC(Pin(pin_num))
            adc.atten(ADC.ATTN_11DB)
            value = adc.read()
            print(f"Pin {pin_num}: {value}")
        except Exception as e:
            print(f"Pin {pin_num} error: {e}")
    time.sleep(1)

# Запуск теста
i = 0
while True:
  i += 1
  test_adc_pins()
  print("---", i)
