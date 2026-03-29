import machine, onewire, ds18x20, time
from machine import ADC, Pin

GOL = True

HW_PIN = 2
DS_PIN = 3

def read_soil_moisture(raw_value):    
    # Калибровка для HW390
    # Сухая почва: высокое значение (~4095)
    # Влажная почва: низкое значение (~1500)
    dry_value = 3333
    wet_value = 1111
    if GOL:
      return raw_value
    
    # Ограничение значений
    raw_value = max(min(raw_value, dry_value), wet_value)
    
    # Преобразование в проценты
    moisture_percent = 100 - ((raw_value - wet_value) / (dry_value - wet_value)) * 100
    
    return max(0, min(100, moisture_percent))


time.sleep(0.5)
print('ABOBA')

# HW-390
adc = ADC(Pin(HW_PIN))
adc.atten(ADC.ATTN_11DB)  # Диапазон 0-3.3V

# DS18B20
ds_pin = machine.Pin(DS_PIN)
ds_sensor = ds18x20.DS18X20(onewire.OneWire(ds_pin))

roms = ds_sensor.scan() # """Слово "ром" и слово "смерть" для вас означают одно и то же, ахахахахаха"""
print('Found DS devices: ', roms)


i = 0
while True:
  # 0) yes
  print("--- iter", i)
  i += 1

  # 1)
  # several DS18B20
  ds_sensor.convert_temp()
  time.sleep_ms(750)
  for rom in roms:
    # print(rom)
    print(f"Temp ({rom.hex()[2:4]}): {ds_sensor.read_temp(rom)}*C")

  # 2)
  # HW-390
  abcde = adc.read()
  print(abcde)
  percent = read_soil_moisture(abcde) # Читаем значение (0-4095)
  percent = int(percent)
  status = "???"
  # Определение статуса
  if percent < 30:
    status = "Dry"
  elif percent < 70:
    status = "Moist (mid)"
  else:
    status = "Wet"
  print(f"{percent}%", status)

  time.sleep(1)
