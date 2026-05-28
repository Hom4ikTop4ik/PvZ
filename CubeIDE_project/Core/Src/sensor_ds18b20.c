#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "main.h"
#include "gpio.h"
#include "core_cm4.h"  /* Для DWT регистров */

#include "printf.h"
#include "sensor_ds18b20.h"

/* Конфигурация пина DS18B20 - PA6 */
#define DS18B20_PORT     GPIOA
#define DS18B20_PIN      GPIO_PIN_6

/* Константы таймингов (мкс) - блокирующие задержки */
#define DS18B20_RESET_PULSE_US      480
#define DS18B20_PRESENCE_WAIT_US    70
#define DS18B20_PRESENCE_SAMPLE_US  410
#define DS18B20_SLOT_US             60
#define DS18B20_RECOVERY_US         1
#define DS18B20_WRITE_1_US            6
#define DS18B20_WRITE_0_US          60
#define DS18B20_READ_SETUP_US       6
#define DS18B20_READ_SAMPLE_US      9

/* State machine variables */
volatile ds18b20_state_t ds18b20_state = DS18B20_STATE_IDLE;
volatile uint32_t ds18b20_state_start_ms = 0;
volatile bool ds18b20_busy = false;
volatile bool ds18b20_result_ready = false;
sensor_reading_t ds18b20_result = {0};
static sensor_history_t history = {0};

/* Internal variables for OneWire protocol */
static uint8_t scratchpad[9] = {0};
static uint8_t byte_idx = 0;

/* Точная задержка в микросекундах через DWT */
static void delay_us(uint32_t us)
{
    /* Включаем DWT счетчик если еще не включен */
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        DWT->CYCCNT = 0;
    }
    
    /* Расчет циклов для задержки */
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    
    /* Ожидание нужного количества циклов */
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}

/* Приватные функции */
static void ds18b20_set_output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // Push-Pull для гарантированного LOW
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
}

static void ds18b20_set_input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
}

/* Программный Open-Drain: управление шиной */
static void ds18b20_drive_bus_low(void)
{
    ds18b20_set_output();           /* Push-Pull режим */
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);  /* Гарантированный LOW */
}

static void ds18b20_release_bus(void)
{
    ds18b20_set_input();            /* Высокий импеданс - HIGH через pull-up */
}

/* Запись бита в OneWire с программным Open-Drain */
static void ds18b20_write_bit(uint8_t bit)
{
    if (bit) {
        /* Запись '1': LOW на 6мкс, затем HIGH */
        ds18b20_drive_bus_low();
        delay_us(DS18B20_WRITE_1_US);
        ds18b20_release_bus();
        delay_us(DS18B20_SLOT_US - DS18B20_WRITE_1_US);
    } else {
        /* Запись '0': LOW на 60мкс, затем HIGH */
        ds18b20_drive_bus_low();
        delay_us(DS18B20_WRITE_0_US);
        ds18b20_release_bus();
        delay_us(DS18B20_RECOVERY_US);
    }
}

/* Чтение бита из OneWire с программным Open-Drain */
static uint8_t ds18b20_read_bit(void)
{
    uint8_t bit;
    
    /* Чтение: LOW на 6мкс, затем HIGH и чтение через 9мкс */
    ds18b20_drive_bus_low();
    delay_us(DS18B20_READ_SETUP_US);
    ds18b20_release_bus();
    delay_us(DS18B20_READ_SAMPLE_US);
    bit = HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN);
    delay_us(DS18B20_SLOT_US - DS18B20_READ_SETUP_US - DS18B20_READ_SAMPLE_US);
    return bit;
}

/* Запись байта */
static void ds18b20_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

/* Чтение байта */
static uint8_t ds18b20_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte >>= 1;
        if (ds18b20_read_bit()) {
            byte |= 0x80;
        }
    }
    return byte;
}

/* Сброс шины OneWire с программным Open-Drain */
static uint8_t ds18b20_reset(void)
{
    uint8_t presence = 0;
    
    /* Сброс: LOW на 480мкс, затем HIGH и ожидание presence */
    ds18b20_drive_bus_low();
    delay_us(DS18B20_RESET_PULSE_US);
    ds18b20_release_bus();
    
    delay_us(DS18B20_PRESENCE_WAIT_US);
    presence = HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN) == GPIO_PIN_RESET;
    delay_us(DS18B20_PRESENCE_SAMPLE_US);
    
    return presence;
}

/* Инициализация датчика */
int ds18b20_init(void)
{
    ds18b20_state = DS18B20_STATE_IDLE;
    ds18b20_result.valid = 0;
    ds18b20_result.error = SENSOR_OK;
    memset(&history, 0, sizeof(history));
    
    /* Инициализация пина в режиме входа (высокий импеданс) */
    ds18b20_set_input();  /* Шина в HIGH через pull-up */
    
    return SENSOR_OK;
}

/* Запуск измерения (internal) */
static int ds18b20_start(void)
{
    /* Сброс шины */
    if (!ds18b20_reset()) {
        return SENSOR_ERR_HW;
    }
    
    /* Запись команды SKIP_ROM */
    ds18b20_set_output();
    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    
    /* Запись команды CONVERT_T */
    ds18b20_write_byte(DS18B20_CMD_CONVERT_T);
    
    return SENSOR_OK;
}

/* Проверка завершения конвертации (internal) */
static int ds18b20_poll(void)
{
    /* Проверка завершения конвертации */
    ds18b20_set_input();
    uint8_t bit = ds18b20_read_bit();
    
    /* Если бит = 0, конвертация еще идет */
    if (bit == 0) {
        return 0;
    }
    
    return 1;  /* Конвертация завершена */
}

/* Чтение scratchpad (internal) */
static int ds18b20_read_scratchpad(void)
{
    /* Сброс шины */
    if (!ds18b20_reset()) {
        return SENSOR_ERR_HW;
    }
    
    /* Запись команды SKIP_ROM */
    ds18b20_set_output();
    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    
    /* Запись команды READ_SCRATCHPAD */
    ds18b20_write_byte(DS18B20_CMD_READ_SCRATCHPAD);
    
    /* Чтение 9 байт scratchpad */
    ds18b20_set_input();
    for (byte_idx = 0; byte_idx < 9; byte_idx++) {
        scratchpad[byte_idx] = ds18b20_read_byte();
    }
    
    /* Парсинг температуры */
    int16_t raw_temp = (int16_t)(scratchpad[1] << 8) | scratchpad[0];
    float temp_c = (float)raw_temp / 16.0f;
    
    ds18b20_result.value = temp_c;
    ds18b20_result.raw = (uint16_t)raw_temp;
    ds18b20_result.valid = 1;
    ds18b20_result.error = SENSOR_OK;
    ds18b20_result.timestamp = HAL_GetTick() / 1000;
    
    return SENSOR_OK;
}

/* Получение результата (internal) */
static int ds18b20_get(sensor_reading_t *out)
{
    if (!out) return -1;
    *out = ds18b20_result;
    return ds18b20_result.valid ? SENSOR_OK : ds18b20_result.error;
}

/* Request measurement - non-blocking */
int ds18b20_request_measurement(void)
{
    if (ds18b20_busy) {
        return -1;  /* Busy */
    }
    ds18b20_busy = true;
    ds18b20_result_ready = false;
    ds18b20_state = DS18B20_STATE_START_CONVERSION;
    ds18b20_state_start_ms = HAL_GetTick();
    return 0;
}

/* Get last result - non-blocking */
int ds18b20_get_result(sensor_reading_t *out)
{
    if (!ds18b20_result_ready || !out) {
        return -1;
    }
    *out = ds18b20_result;
    ds18b20_result_ready = false;
    return 0;
}

/* State machine tick - call every main loop iteration */
void ds18b20_tick(void)
{
    uint32_t now = HAL_GetTick();
    
    switch (ds18b20_state) {
        case DS18B20_STATE_IDLE:
            /* Do nothing, wait for request */
            break;
            
        case DS18B20_STATE_START_CONVERSION:
            if (ds18b20_start() != SENSOR_OK) {
                ds18b20_result.value = DS18B20_ERROR_VALUE;
                ds18b20_result.valid = 0;
                ds18b20_result.error = SENSOR_ERR_HW;
                ds18b20_state = DS18B20_STATE_ERROR;
            } else {
                ds18b20_state = DS18B20_STATE_WAIT_CONVERSION;
                ds18b20_state_start_ms = now;
            }
            break;
            
        case DS18B20_STATE_WAIT_CONVERSION:
            if (ds18b20_poll()) {
                ds18b20_state = DS18B20_STATE_READ_SCRATCHPAD;
            }
            else if (now - ds18b20_state_start_ms > DS18B20_CONVERSION_TIMEOUT_MS) {
                ds18b20_result.value = DS18B20_ERROR_VALUE;
                ds18b20_result.valid = 0;
                ds18b20_result.error = SENSOR_ERR_TIMEOUT;
                ds18b20_state = DS18B20_STATE_ERROR;
            }
            break;
            
        case DS18B20_STATE_READ_SCRATCHPAD:
            if (ds18b20_read_scratchpad() == SENSOR_OK) {
                ds18b20_result_ready = true;
                ds18b20_busy = false;
                ds18b20_state = DS18B20_STATE_IDLE;
            } else {
                ds18b20_result.value = DS18B20_ERROR_VALUE;
                ds18b20_result.valid = 0;
                ds18b20_result.error = SENSOR_ERR_HW;
                ds18b20_state = DS18B20_STATE_ERROR;
            }
            break;
            
        case DS18B20_STATE_ERROR:
            ds18b20_result_ready = true;
            ds18b20_busy = false;
            ds18b20_state = DS18B20_STATE_IDLE;
            break;
    }
	PRINTF("ds18b20_result_ready - %d\r\n", ds18b20_result_ready);
}

/* Получение истории */
int ds18b20_get_history(sensor_history_t *out)
{
    if (!out) return -1;
    *out = history;
    return SENSOR_OK;
}

/* Очистка истории */
void ds18b20_clear_history(void)
{
    memset(&history, 0, sizeof(history));
}
