#ifndef SENSOR_COMMON_H
#define SENSOR_COMMON_H

#include <stdint.h>
#include "stm32l4xx_hal.h"

/* Коды ошибок датчиков */
#define SENSOR_OK           0
#define SENSOR_ERR_HW      -500
#define SENSOR_ERR_TIMEOUT -501
#define SENSOR_ERR_CRC     -502

/* Настраиваемый размер буфера истории */
#ifndef SENSOR_HISTORY_SIZE
#define SENSOR_HISTORY_SIZE 32
#endif

/* Структура для хранения показаний датчика */
typedef struct {
    float value;           /* откалиброванное значение (влажность %, температура °C, напряжение V) */
    uint16_t raw;          /* сырое значение ADC/I2C */
    uint8_t valid;         /* 0 = невалидно, 1 = готово */
    int16_t error;         /* 0 = OK, отрицательные = код ошибки */
    uint32_t timestamp;    /* время RTC в секундах */
} sensor_reading_t;

/* Буфер истории показаний */
typedef struct {
    sensor_reading_t buffer[SENSOR_HISTORY_SIZE];
    uint8_t head;          /* позиция записи */
    uint8_t count;         /* количество валидных записей */
} sensor_history_t;

/* Вспомогательная функция добавления записи в историю */
static inline void sensor_history_add(sensor_history_t *hist, const sensor_reading_t *reading)
{
    if (!hist || !reading) return;
    hist->buffer[hist->head] = *reading;
    hist->head = (hist->head + 1) % SENSOR_HISTORY_SIZE;
    if (hist->count < SENSOR_HISTORY_SIZE) {
        hist->count++;
    }
}

#endif /* SENSOR_COMMON_H */
