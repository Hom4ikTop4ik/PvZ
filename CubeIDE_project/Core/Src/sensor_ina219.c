#include "sensor_ina219.h"
#include "i2c.h"
#include "main.h"
#include <string.h>

/* Внешний дескриптор I2C из i2c.c */
extern I2C_HandleTypeDef hi2c1;

/* Состояния модуля */
typedef enum {
    INA219_IDLE = 0,       /* ожидание */
    INA219_MEASURING,      /* измерение */
    INA219_DONE           /* завершено */
} ina219_state_t;

static ina219_state_t state = INA219_IDLE;
static sensor_reading_t current_reading = {0};
static sensor_history_t history = {0};
static uint32_t start_time = 0;

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

/* Заглушки power gating */
void ina219_power_on(void)
{
    /* Заглушка */
}

void ina219_power_off(void)
{
    /* Заглушка */
}

/* Инициализация датчика */
int ina219_init(void)
{
    state = INA219_IDLE;
    memset(&current_reading, 0, sizeof(current_reading));
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

/* Запуск измерения */
int ina219_start(void)
{
    if (state != INA219_IDLE) {
        return -1;
    }
    
    state = INA219_MEASURING;
    start_time = HAL_GetTick();
    return SENSOR_OK;
}

/* Проверка состояния измерения */
int ina219_poll(void)
{
    if (state == INA219_IDLE) {
        return 1;
    }
    
    if (state == INA219_MEASURING) {
        uint16_t bus_voltage_reg;
        
        if (ina219_read_register(INA219_REG_BUS_VOLT, &bus_voltage_reg) != SENSOR_OK) {
            current_reading.error = SENSOR_ERR_HW;
            current_reading.value = -500.0f;
            current_reading.valid = 0;
            state = INA219_IDLE;
            return SENSOR_ERR_HW;
        }
        
        /* Извлечение напряжения (сдвиг на 3 бита вправо, умножение на 4мВ) */
        uint16_t voltage_raw = bus_voltage_reg >> 3;
        float voltage_v = (float)voltage_raw * 0.004f;
        
        /* Расчет процента заряда */
        float voltage_mv = voltage_v * 1000.0f;
        float percentage = 0.0f;
        
        if (voltage_mv >= BATTERY_MAX_VOLTAGE_MV) {
            percentage = 100.0f;
        } else if (voltage_mv <= BATTERY_MIN_VOLTAGE_MV) {
            percentage = 0.0f;
        } else {
            percentage = 100.0f * (voltage_mv - BATTERY_MIN_VOLTAGE_MV) / 
                        (BATTERY_MAX_VOLTAGE_MV - BATTERY_MIN_VOLTAGE_MV);
        }
        
        current_reading.value = percentage;  /* Сохраняем процент */
        current_reading.raw = voltage_raw;
        current_reading.valid = 1;
        current_reading.error = SENSOR_OK;
        current_reading.timestamp = HAL_GetTick() / 1000;
        
        sensor_history_add(&history, &current_reading);
        
        state = INA219_DONE;
        return 1;
    }
    
    return 1;
}

/* Получение результата */
int ina219_get(sensor_reading_t *out)
{
    if (!out) return -1;
    
    *out = current_reading;
    
    if (state == INA219_DONE) {
        state = INA219_IDLE;
    }
    
    return current_reading.valid ? SENSOR_OK : current_reading.error;
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

/* Прямое чтение напряжения батареи */
float ina219_read_voltage(void)
{
    uint16_t bus_voltage_reg;
    
    if (ina219_read_register(INA219_REG_BUS_VOLT, &bus_voltage_reg) != SENSOR_OK) {
        return -1.0f;
    }
    
    uint16_t voltage_raw = bus_voltage_reg >> 3;
    return (float)voltage_raw * 0.004f;
}
