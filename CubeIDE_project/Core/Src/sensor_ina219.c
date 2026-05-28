#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "i2c.h"
#include "main.h"

#include "sensor_ina219.h"
#include "printf.h"

/* Внешний дескриптор I2C из i2c.c */
extern I2C_HandleTypeDef hi2c1;

/* State machine variables */
ina219_state_t ina219_state = INA219_STATE_IDLE;
uint32_t ina219_state_start_ms = 0;
bool ina219_busy = false;
bool ina219_result_ready = false;
sensor_reading_t ina219_result = {0};
static sensor_history_t history = {0};

/* Приватные функции */
/* Чтение регистра INA219 */
static int ina219_read_register(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    
    if (HAL_I2C_Mem_Read(&hi2c1, INA219_ADDR << 1, reg, 
                         I2C_MEMADD_SIZE_8BIT, buf, 2, 100) != HAL_OK) {
        return SENSOR_ERR_HW;
    }
    
    *value = ((uint16_t)buf[0] << 8) | buf[1];
    return SENSOR_OK;
}

/* Запись регистра INA219 */
static int ina219_write_register(uint8_t reg, uint16_t value)
{
    uint8_t buf[2] = {(value >> 8) & 0xFF, value & 0xFF};
    
    if (HAL_I2C_Mem_Write(&hi2c1, INA219_ADDR << 1, reg,
                          I2C_MEMADD_SIZE_8BIT, buf, 2, 100) != HAL_OK) {
        return SENSOR_ERR_HW;
    }
    
    return SENSOR_OK;
}

/* Инициализация датчика */
int ina219_init(void)
{
    ina219_state = INA219_STATE_IDLE;
    ina219_result.valid = 0;
    ina219_result.error = SENSOR_OK;
    memset(&history, 0, sizeof(history));
    
    /* Конфигурация INA219 */
    /* Сброс и установка режима 32V 2A */
    if (ina219_write_register(INA219_REG_CONFIG, 0x399F) != SENSOR_OK) {
        return SENSOR_ERR_HW;
    }
    
    /* Установка калибровки для шунта 0.1 Ом */
    /* Current_LSB = 0.0001 A/bit, Power_LSB = 0.002 W/bit */
    if (ina219_write_register(INA219_REG_CALIBRATION, 0x1000) != SENSOR_OK) {
        return SENSOR_ERR_HW;
    }
    
    HAL_Delay(10);  /* Небольшая задержка для установки конфигурации */
    
    return SENSOR_OK;
}

/* Запуск измерения (internal) */
static int ina219_start(void)
{
    /* No-op for INA219 - measurements are immediate */
    return SENSOR_OK;
}

/* Проверка состояния измерения (internal) */
static int ina219_poll(void)
{
    /* No-op for INA219 - measurements are immediate */
    return 1;
}

/* Чтение тока (internal) */
static int ina219_read_current(float *current)
{
    uint16_t current_reg;
    
    if (ina219_read_register(INA219_REG_CURRENT, &current_reg) != SENSOR_OK) {
        return SENSOR_ERR_HW;
    }
    
    *current = (float)current_reg * 0.0001f;  /* Current_LSB = 0.0001 A/bit */
    return SENSOR_OK;
}

/* Получение результата (internal) */
static int ina219_get(sensor_reading_t *out)
{
    if (!out) return -1;
    *out = ina219_result;
    return ina219_result.valid ? SENSOR_OK : ina219_result.error;
}

/* Request measurement - non-blocking */
int ina219_request_measurement(void)
{
    if (ina219_busy) {
        return -1;  /* Busy */
    }
    ina219_busy = true;
    ina219_result_ready = false;
    ina219_state = INA219_STATE_READ_VOLTAGE;
    ina219_state_start_ms = HAL_GetTick();
    return 0;
}

/* Get last result - non-blocking */
int ina219_get_result(sensor_reading_t *out)
{
    if (!ina219_result_ready || !out) {
        return -1;
    }
    *out = ina219_result;
    ina219_result_ready = false;
    return 0;
}

/* State machine tick - call every main loop iteration */
void ina219_tick(void)
{
    switch (ina219_state) {
        case INA219_STATE_IDLE:
            /* Do nothing, wait for request */
            break;
            
        case INA219_STATE_READ_VOLTAGE:
            {
                float voltage;
                if (ina219_read_voltage(&voltage) == SENSOR_OK) {
                    /* Calculate battery percentage */
                    float voltage_mv = voltage * 1000.0f;
                    float percentage = 0.0f;
                    
                    if (voltage_mv >= BATTERY_MAX_VOLTAGE_MV) {
                        percentage = 100.0f;
                    } else if (voltage_mv <= BATTERY_MIN_VOLTAGE_MV) {
                        percentage = 0.0f;
                    } else {
                        percentage = 100.0f * (voltage_mv - BATTERY_MIN_VOLTAGE_MV) / 
                                    (BATTERY_MAX_VOLTAGE_MV - BATTERY_MIN_VOLTAGE_MV);
                    }
                    
                    ina219_result.value = percentage;
                    ina219_result.raw = (uint16_t)(voltage_mv / 4.0f);  /* Store raw voltage */
                    ina219_state = INA219_STATE_READ_CURRENT;
                } else {
                    ina219_result.value = INA219_ERROR_VOLTAGE;
                    ina219_result.valid = 0;
                    ina219_result.error = SENSOR_ERR_HW;
                    ina219_state = INA219_STATE_ERROR;
                }
            }
            break;
            
        case INA219_STATE_READ_CURRENT:
            {
                float current;
                if (ina219_read_current(&current) == SENSOR_OK) {
                    ina219_result.valid = 1;
                    ina219_result.error = SENSOR_OK;
                    ina219_result.timestamp = HAL_GetTick() / 1000;
                    ina219_result_ready = true;
                    ina219_busy = false;
                    ina219_state = INA219_STATE_IDLE;
                } else {
                    /* Voltage was read successfully, but current failed - still return partial result */
                    ina219_result.valid = 1;
                    ina219_result.error = SENSOR_ERR_HW;
                    ina219_result.timestamp = HAL_GetTick() / 1000;
                    ina219_result_ready = true;
                    ina219_busy = false;
                    ina219_state = INA219_STATE_IDLE;
                }
            }
            break;
            
        case INA219_STATE_ERROR:
            ina219_result_ready = true;
            ina219_busy = false;
            ina219_state = INA219_STATE_IDLE;
            break;
    }
	PRINTF("ina219_result_ready - %d\r\n", ina219_result_ready);
}

/* Получение истории */
int ina219_get_history(sensor_history_t *out)
{
    if (!out) return -1;
    *out = history;
    return SENSOR_OK;
}

/* Очистка истории */
void ina219_clear_history(void)
{
    memset(&history, 0, sizeof(history));
}

/* Прямое чтение напряжения батареи - public function for measure_state() */
int ina219_read_voltage(float *voltage)
{
    if (!voltage) return -1;
    
    uint16_t bus_voltage_reg;
    
    if (ina219_read_register(INA219_REG_BUS_VOLT, &bus_voltage_reg) != SENSOR_OK) {
        return -1;
    }
    
    uint16_t voltage_raw = bus_voltage_reg >> 3;
    *voltage = (float)voltage_raw * 0.004f;
    return 0;
}

