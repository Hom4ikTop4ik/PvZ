from machine import Pin, I2C
import time

pins_to_try = [
    (9, 8),
    (8, 9),
    (6, 5),
    (5, 6),
    (4, 3),
    (3, 4),
]

for scl_pin, sda_pin in pins_to_try:
    try:
        print("Trying SCL={}, SDA={}".format(scl_pin, sda_pin))
        i2c = I2C(0, scl=Pin(scl_pin), sda=Pin(sda_pin), freq=100000)
        devices = i2c.scan()
        print("Found:", [hex(x) for x in devices])
    except Exception as e:
        print("Error:", e)
    print("-" * 30)
    time.sleep(1)

