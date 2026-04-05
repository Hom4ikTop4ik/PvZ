#include "sensor_hw390.h"
#include "adc.h"
#include "main.h"
#include <string.h>

/* Переменные калибровки в runtime */
uint16_t hw390_raw_dry = HW390_RAW_DRY_DEFAULT;
uint16_t hw390_raw_wet = HW390_RAW_WET_DEFAULT;

/* Состояния модуля */
typedef enum {
    HW390_IDLE = 0,      /* ожидание */
    HW390_MEASURING,     /* измерение */
    HW390_DONE          /* завершено */
} hw390_state_t;

static hw390_state_t state = HW390_IDLE;
static sensor_reading_t current_reading = {0};
static sensor_history_t history = {0};
static uint32_t start_time = 0;

/* Внешний дескриптор ADC из adc.c */
extern ADC_HandleTypeDef hadc1;

/* Заглушки power gating */
void hw390_power_on(void)
{
    /* Заглушка для будущей реализации power gating */
}

void hw390_power_off(void)
{
    /* Заглушка для будущей реализации power gating */
}

/* Инициализация датчика */
int hw390_init(void)
{
    state = HW390_IDLE;
    current_reading.valid = 0;
    current_reading.error = SENSOR_OK;
    memset(&history, 0, sizeof(history));
    return SENSOR_OK;
}

/* Запуск измерения */
int hw390_start(void)
{
    if (state != HW390_IDLE) {
        return -1;  /* Занят */
    }
    
    /* Запуск ADC преобразования */
    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        current_reading.error = SENSOR_ERR_HW;
        return SENSOR_ERR_HW;
    }
    
    state = HW390_MEASURING;
    start_time = HAL_GetTick();
    return SENSOR_OK;
}

/* Проверка состояния измерения */
int hw390_poll(void)
{
    if (state == HW390_IDLE) {
        return 1;  /* Ничего не делать, считаем "готовым" */
    }
    
    if (state == HW390_MEASURING) {
        /* Проверка завершения ADC преобразования */
        if (HAL_ADC_PollForConversion(&hadc1, 0) == HAL_OK) {
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
            current_reading.value = humidity;
            current_reading.raw = (uint16_t)raw;
            current_reading.valid = 1;
            current_reading.error = SENSOR_OK;
            current_reading.timestamp = HAL_GetTick() / 1000;  /* Приблизительное время в секундах */
            
            /* Добавление в историю */
            sensor_history_add(&history, &current_reading);
            
            state = HW390_DONE;
            return 1;  /* Готов */
        }
        
        /* Проверка таймаута (макс 100мс) */
        if (HAL_GetTick() - start_time > 100) {
            HAL_ADC_Stop(&hadc1);
            current_reading.valid = 0;
            current_reading.error = SENSOR_ERR_TIMEOUT;
            current_reading.value = -500.0f;
            state = HW390_IDLE;
            return SENSOR_ERR_TIMEOUT;
        }
        
        return 0;  /* Занят */
    }
    
    return 1;  /* Завершено */
}

/* Получение результата */
int hw390_get(sensor_reading_t *out)
{
    if (!out) return -1;
    
    *out = current_reading;
    
    /* Сброс состояния для следующего измерения */
    if (state == HW390_DONE) {
        state = HW390_IDLE;
    }
    
    return current_reading.valid ? SENSOR_OK : current_reading.error;
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
