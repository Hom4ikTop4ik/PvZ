#ifndef SENSOR_INA219_H
#define SENSOR_INA219_H

#include <stdbool.h>
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

/* Error values */
#define INA219_ERROR_VOLTAGE 0.0f
#define INA219_ERROR_CURRENT -999.0f

/* State machine timeout */
#define INA219_I2C_TIMEOUT_MS 100

/* State machine states */
typedef enum {
    INA219_STATE_IDLE = 0,
    INA219_STATE_READ_VOLTAGE,
    INA219_STATE_READ_CURRENT,
    INA219_STATE_ERROR
} ina219_state_t;

/* State machine variables (extern for logging) */
extern ina219_state_t ina219_state;
extern uint32_t ina219_state_start_ms;
extern bool ina219_busy;
extern bool ina219_result_ready;

/* API датчика */
int ina219_init(void);

/* Request measurement - non-blocking */
int ina219_request_measurement(void);

/* Get last result - non-blocking */
int ina219_get_result(sensor_reading_t *out);

/* State machine tick - call every main loop iteration */
void ina219_tick(void);

/* История */
int ina219_get_history(sensor_history_t *out);
void ina219_clear_history(void);

/* Прямое чтение напряжения */
int ina219_read_voltage(float *voltage);  /* Возвращает напряжение в Вольтах */

#endif /* SENSOR_INA219_H */
