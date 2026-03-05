from machine import Pin, I2C
import time
import struct


class INA219:
    REG_CONFIG = 0x00
    REG_SHUNT_VOLTAGE = 0x01
    REG_BUS_VOLTAGE = 0x02
    REG_POWER = 0x03
    REG_CURRENT = 0x04
    REG_CALIBRATION = 0x05

    def __init__(self, i2c, address=0x40):
        self.i2c = i2c
        self.address = address

        self.current_lsb = 0.0001   # 100 uA/bit
        self.power_lsb = 0.002      # 2 mW/bit
        self.calibration_value = 4096

        self.configure()

    def write_register(self, reg, value):
        data = struct.pack(">H", value)
        self.i2c.writeto_mem(self.address, reg, data)

    def read_register(self, reg):
        data = self.i2c.readfrom_mem(self.address, reg, 2)
        return struct.unpack(">H", data)[0]

    def read_signed(self, reg):
        value = self.read_register(reg)
        if value > 32767:
            value -= 65536
        return value

    def configure(self):
        self.write_register(self.REG_CALIBRATION, self.calibration_value)

        # 32V range, 320mV shunt range, 12-bit ADC, continuous
        config = 0x399F
        self.write_register(self.REG_CONFIG, config)

    def get_shunt_voltage_mv(self):
        raw = self.read_signed(self.REG_SHUNT_VOLTAGE)
        return raw * 0.01  # 10 uV per bit = 0.01 mV

    def get_bus_voltage_v(self):
        raw = self.read_register(self.REG_BUS_VOLTAGE)
        return ((raw >> 3) * 4) / 1000.0  # 4 mV per bit

    def get_current_ma(self):
        self.write_register(self.REG_CALIBRATION, self.calibration_value)
        raw = self.read_signed(self.REG_CURRENT)
        return raw * self.current_lsb * 1000.0

    def get_power_w(self):
        self.write_register(self.REG_CALIBRATION, self.calibration_value)
        raw = self.read_register(self.REG_POWER)
        return raw * self.power_lsb

    def get_load_voltage_v(self):
        return self.get_bus_voltage_v() + self.get_shunt_voltage_mv() / 1000.0


i2c = I2C(0, scl=Pin(3), sda=Pin(4), freq=100000)

print("I2C scan:", [hex(addr) for addr in i2c.scan()])

ina219 = INA219(i2c)

while True:
    try:
        bus_voltage = ina219.get_bus_voltage_v()
        shunt_voltage = ina219.get_shunt_voltage_mv()
        current = ina219.get_current_ma()
        power = ina219.get_power_w()
        load_voltage = ina219.get_load_voltage_v()

        print("Bus Voltage:  {:.3f} V".format(bus_voltage))
        print("Shunt Volt:   {:.3f} mV".format(shunt_voltage))
        print("Load Voltage: {:.3f} V".format(load_voltage))
        print("Current:      {:.3f} mA".format(current))
        print("Power:        {:.3f} W".format(power))
        print("-----------------------------")

    except Exception as e:
        print("Error:", e)

    time.sleep(1)
