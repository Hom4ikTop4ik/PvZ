#ifndef SENSOR_INA219_H
#define SENSOR_INA219_H

#include "sensor_common.h"

/* I2C адрес */
#define INA219_ADDR             0x40

/* Адреса регистров */
#define INA219_REG_CONFIG       0x00
#define INA219_REG_SHUNT_VOLT   0x01
#define INA219_REG_BUS_VOLT     0x02
#define INA219_REG_POWER        0x03
#define INA219_REG_CURRENT      0x04
#define INA219_REG_CALIBRATION  0x05

/* Пороги напряжения батареи для LiFePO4 (мВ) */
#define BATTERY_MIN_VOLTAGE_MV  2500
#define BATTERY_MAX_VOLTAGE_MV  3650

/* Заглушки power gating */
void ina219_power_on(void);
void ina219_power_off(void);

/* API датчика */
int ina219_init(void);
int ina219_start(void);
int ina219_poll(void);
int ina219_get(sensor_reading_t *out);

/* История */
int ina219_get_history(sensor_history_t *out);
void ina219_clear_history(void);

/* Прямое чтение напряжения */
float ina219_read_voltage(void);  /* Возвращает напряжение в Вольтах */

#endif /* SENSOR_INA219_H */
