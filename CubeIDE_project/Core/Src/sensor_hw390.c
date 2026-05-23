#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "sensor_hw390.h"
#include "hw390_flash.h"
#include "adc.h"
#include "main.h"
#include "printf.h"

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
void hw390_power_on(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
}

void hw390_power_off(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
}

/* Инициализация датчика */
int hw390_init(void)
{
    uint16_t loaded_dry, loaded_wet;
    if (hw390_flash_load_calibration(&loaded_dry, &loaded_wet) == 0) {
        hw390_set_calibration(loaded_dry, loaded_wet);
    } else {
        // default HW390 values
        hw390_set_calibration(HW390_RAW_DRY_DEFAULT, HW390_RAW_WET_DEFAULT);
    }

    PRINTF("[HW390] Values: min-wet=%d  max-dry=%d\r\n", hw390_raw_wet, hw390_raw_dry);

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

float hw390_normalize(uint16_t raw) {
    int32_t dry = hw390_raw_dry;
    int32_t wet = hw390_raw_wet;

    if (dry <= wet) return HW390_ERROR_VALUE;
    if (raw >= dry) return 0.0f;        // максимально сухо
    if (raw <= wet) return 100.0f;      // максимально влажно
    // линейная интерполяция между dry и wet:
    return 100.0f * (dry - raw) / (dry - wet);
}

/* Проверка состояния измерения */
static int hw390_poll(void)
{
    /* Проверка завершения ADC преобразования */
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK) {
        uint32_t raw = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);

        /* Расчет влажности */
        float humidity = hw390_normalize(raw);

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
    PRINTF("hw390_result_ready - %d\r\n", hw390_result_ready);
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


void hw390_run_calibration(void) {
    PRINTF("[HW390] Start calibration\r\n");
    // Включить питание сенсора
    hw390_power_on();
    HAL_Delay(HW390_POWER_ON_DELAY_MS); // Дать сенсору прогреться


    uint32_t start_time = HAL_GetTick();
    uint16_t min_raw = 0xFFFF;
    uint16_t max_raw = 0x0000;
    uint16_t raw = 0;

    while ((HAL_GetTick() - start_time) < HW390_CALIBRATION_DURATION_MS) {
        // Запустить преобразование
        if (HAL_ADC_Start(&hadc1) == HAL_OK) {
            if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
                raw = HAL_ADC_GetValue(&hadc1);

                if (raw < min_raw) min_raw = raw;
                if (raw > max_raw) max_raw = raw;

                PRINTF("[Калибровка] HW390: raw=%u, min=%u, max=%u\r\n", raw, min_raw, max_raw);
            }
            HAL_ADC_Stop(&hadc1);
        }

        HAL_Delay(HW390_CALIBRATION_POLL_INTERVAL_MS);
    }

    hw390_power_off();

    PRINTF("[HW390] Calibrationd done: min(wet)=%u, max(dry)=%u\r\n", min_raw, max_raw);


    // Сохранить во FLASH
    if (hw390_flash_save_calibration(max_raw, min_raw) == 0) {
        PRINTF("[HW390] Calibration wrote into FLASH!\r\n");
    } else {
        PRINTF("[HW390] FLASH Error: error while writing calibration into FLASH!\r\n");
    }

    HAL_Delay(500);
    PRINTF("Reboot...\r\n");
    HAL_NVIC_SystemReset();
}

void hw390_get_calibration(uint16_t *dry, uint16_t *wet) {
    *dry = hw390_raw_dry;
    *wet = hw390_raw_wet;
}
/* Установка калибровки */
void hw390_set_calibration(uint16_t dry, uint16_t wet)
{
    hw390_raw_dry = dry;
    hw390_raw_wet = wet;
}
