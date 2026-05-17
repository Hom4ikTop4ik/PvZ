#include "sensor_hw390.h"
#include "adc.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Переменные калибровки в runtime */
uint16_t hw390_raw_dry = HW390_RAW_DRY_DEFAULT;
uint16_t hw390_raw_wet = HW390_RAW_WET_DEFAULT;

/* State machine variables */
volatile hw390_state_t hw390_state = HW390_STATE_IDLE;
volatile uint32_t hw390_state_start_ms = 0;
volatile bool hw390_busy = false;
volatile bool hw390_result_ready = false;
sensor_reading_t hw390_result = {0};
static sensor_history_t history = {0};

/* Внешний дескриптор ADC из adc.c */
extern ADC_HandleTypeDef hadc1;

/* Power gating (N-channel MOSFET, active HIGH): SET(3.3v) = power on, RESET(0.0v) = power off */
static void hw390_power_on(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
}

static void hw390_power_off(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
}

/* Инициализация датчика */
int hw390_init(void)
{
    hw390_state = HW390_STATE_IDLE;
    hw390_result.valid = 0;
    hw390_result.error = SENSOR_OK;
    return SENSOR_OK;
}

/* Запуск измерения */
static int hw390_start(void)
{
    /* Запуск ADC преобразования */
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return SENSOR_ERR_HW;
    }
    return SENSOR_OK;
}

/* Проверка состояния измерения */
static int hw390_poll(void)
{
    /* Проверка завершения ADC преобразования */
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK) {
        uint32_t raw = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        
        /* Расчет влажности */
        float humidity = 0.0f;
        if (hw390_raw_wet != hw390_raw_dry) {
            humidity = 100.0f * (float)(hw390_raw_dry - raw) / 
                      (float)(hw390_raw_dry - hw390_raw_wet);
        }
        
        /* Ограничение диапазоном 0-100% */
        if (humidity < 0.0f) humidity = 0.0f;
        if (humidity > 100.0f) humidity = 100.0f;
        
        /* Сохранение результата */
        hw390_result.value = humidity;
        hw390_result.raw = (uint16_t)raw;
        hw390_result.valid = 1;
        hw390_result.error = SENSOR_OK;
        hw390_result.timestamp = HAL_GetTick() / 1000;
        
        return 1;  /* Готов */
    }
    
    return 0;  /* Занят */
}

/* Получение результата (internal) */
static int hw390_get(sensor_reading_t *out)
{
    if (!out) return -1;
    *out = hw390_result;
    return hw390_result.valid ? SENSOR_OK : hw390_result.error;
}

/* Request measurement - non-blocking */
int hw390_request_measurement(void)
{
    if (hw390_busy) {
        return -1;  /* Busy */
    }
    hw390_busy = true;
    hw390_result_ready = false;
    hw390_state = HW390_STATE_POWER_ON_DELAY;
    hw390_state_start_ms = HAL_GetTick();
    return 0;
}

/* Get last result - non-blocking */
int hw390_get_result(sensor_reading_t *out)
{
    if (!hw390_result_ready || !out) {
        return -1;
    }
    *out = hw390_result;
    hw390_result_ready = false;
    return 0;
}

/* State machine tick - call every main loop iteration */
void hw390_tick(void)
{
    uint32_t now = HAL_GetTick();
    
    switch (hw390_state) {
        case HW390_STATE_IDLE:
            /* Do nothing, wait for request */
            break;
            
        case HW390_STATE_POWER_ON_DELAY:
            if (now - hw390_state_start_ms >= HW390_POWER_ON_DELAY_MS) {
                hw390_power_on();
                if (hw390_start() != SENSOR_OK) {
                    hw390_result.value = HW390_ERROR_VALUE;
                    hw390_result.valid = 0;
                    hw390_result.error = SENSOR_ERR_HW;
                    hw390_state = HW390_STATE_ERROR;
                } else {
                    hw390_state = HW390_STATE_MEASURING;
                    hw390_state_start_ms = now;
                }
            }
            break;
            
        case HW390_STATE_MEASURING:
            if (hw390_poll()) {
                hw390_get(&hw390_result);
                hw390_state = HW390_STATE_READ;
            } else if (now - hw390_state_start_ms > HW390_MEASURING_TIMEOUT_MS) {
                hw390_result.value = HW390_ERROR_VALUE;
                hw390_result.valid = 0;
                hw390_result.error = SENSOR_ERR_TIMEOUT;
                hw390_state = HW390_STATE_ERROR;
            }
            break;
            
        case HW390_STATE_READ:
            hw390_power_off();
            hw390_result_ready = true;
            hw390_busy = false;
            hw390_state = HW390_STATE_IDLE;
            break;
            
        case HW390_STATE_ERROR:
            hw390_power_off();
            hw390_result_ready = true;
            hw390_busy = false;
            hw390_state = HW390_STATE_IDLE;
            break;
    }
	printf("hw390_result_ready - %d\r\n", hw390_result_ready);
}

/* Получение истории */
int hw390_get_history(sensor_history_t *out)
{
    if (!out) return -1;
    *out = history;
    return SENSOR_OK;
}

/* Очистка истории */
void hw390_clear_history(void)
{
    memset(&history, 0, sizeof(history));
}

/* Получение значений калибровки */
uint16_t hw390_get_raw_dry(void)
{
    return hw390_raw_dry;
}

uint16_t hw390_get_raw_wet(void)
{
    return hw390_raw_wet;
}

/* Установка калибровки */
void hw390_set_calibration(uint16_t dry, uint16_t wet)
{
    hw390_raw_dry = dry;
    hw390_raw_wet = wet;
}
