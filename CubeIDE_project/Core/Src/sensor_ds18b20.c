#include "sensor_ds18b20.h"
#include "main.h"
#include "gpio.h"
#include <string.h>
#include "core_cm4.h"  /* Для DWT регистров */

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

/* Состояния модуля */
typedef enum {
    DS18B20_IDLE = 0,      /* ожидание */
    DS18B20_RESET,         /* сброс шины */
    DS18B20_WRITING,       /* запись команд */
    DS18B20_CONVERTING,    /* конвертация температуры */
    DS18B20_READING,       /* чтение данных */
    DS18B20_DONE          /* завершено */
} ds18b20_state_t;

typedef enum {
    DS18B20_STEP_RESET = 0,     /* шаг сброса */
    DS18B20_STEP_SKIP_ROM,      /* пропустить ROM */
    DS18B20_STEP_CONVERT,       /* конвертировать температуру */
    DS18B20_STEP_DELAY,         /* задержка конвертации */
    DS18B20_STEP_READ_RESET,    /* сброс перед чтением */
    DS18B20_STEP_READ_SKIP,     /* пропустить ROM перед чтением */
    DS18B20_STEP_READ_CMD,      /* команда чтения */
    DS18B20_STEP_READ_DATA      /* чтение данных */
} ds18b20_step_t;

static ds18b20_state_t state = DS18B20_IDLE;
static ds18b20_step_t step = DS18B20_STEP_RESET;
static sensor_reading_t current_reading = {0};
static sensor_history_t history = {0};
static uint32_t convert_start_time = 0;
static uint8_t scratchpad[9] = {0};
static uint8_t byte_idx = 0;
static uint8_t bit_idx = 0;
static uint8_t current_byte = 0;

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

/* Заглушки power gating */
void ds18b20_power_on(void)
{
    /* Заглушка */
}

void ds18b20_power_off(void)
{
    /* Заглушка */
}

/* Инициализация датчика */
int ds18b20_init(void)
{
    state = DS18B20_IDLE;
    step = DS18B20_STEP_RESET;
    memset(&current_reading, 0, sizeof(current_reading));
    memset(&history, 0, sizeof(history));
    
    /* Инициализация пина в режиме входа (высокий импеданс) */
    ds18b20_set_input();  /* Шина в HIGH через pull-up */
    
    return SENSOR_OK;
}

/* Запуск измерения */
int ds18b20_start(void)
{
    if (state != DS18B20_IDLE) {
        return -1;
    }
    
    state = DS18B20_RESET;
    step = DS18B20_STEP_RESET;
    return SENSOR_OK;
}

/* Проверка состояния измерения */
int ds18b20_poll(void)
{
    switch (state) {
        case DS18B20_IDLE:
            return 1;
            
        case DS18B20_RESET:
            if (!ds18b20_reset()) {
                current_reading.error = SENSOR_ERR_HW;
                current_reading.value = -500.0f;
                current_reading.valid = 0;
                state = DS18B20_IDLE;
                return SENSOR_ERR_HW;
            }
            state = DS18B20_WRITING;
            step = DS18B20_STEP_SKIP_ROM;
            byte_idx = 0;
            bit_idx = 0;
            current_byte = 0;
            return 0;
            
        case DS18B20_WRITING:
            ds18b20_set_output();
            
            switch (step) {
                case DS18B20_STEP_SKIP_ROM:
                    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
                    step = DS18B20_STEP_CONVERT;
                    return 0;
                    
                case DS18B20_STEP_CONVERT:
                    ds18b20_write_byte(DS18B20_CMD_CONVERT_T);
                    state = DS18B20_CONVERTING;
                    convert_start_time = HAL_GetTick();
                    return 0;
                    
                default:
                    break;
            }
            return 0;
            
        case DS18B20_CONVERTING:
            /* Проверка времени конвертации (неблокирующее ожидание) */
            if (HAL_GetTick() - convert_start_time < DS18B20_CONV_TIME_MS) {
                return 0;  /* Все еще конвертирует */
            }
            
            /* Начало фазы чтения */
            state = DS18B20_READING;
            step = DS18B20_STEP_READ_RESET;
            byte_idx = 0;
            return 0;
            
        case DS18B20_READING:
            switch (step) {
                case DS18B20_STEP_READ_RESET:
                    if (!ds18b20_reset()) {
                        current_reading.error = SENSOR_ERR_HW;
                        current_reading.value = -500.0f;
                        current_reading.valid = 0;
                        state = DS18B20_IDLE;
                        return SENSOR_ERR_HW;
                    }
                    step = DS18B20_STEP_READ_SKIP;
                    return 0;
                    
                case DS18B20_STEP_READ_SKIP:
                    ds18b20_set_output();
                    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
                    step = DS18B20_STEP_READ_CMD;
                    return 0;
                    
                case DS18B20_STEP_READ_CMD:
                    ds18b20_write_byte(DS18B20_CMD_READ_SCRATCHPAD);
                    step = DS18B20_STEP_READ_DATA;
                    byte_idx = 0;
                    return 0;
                    
                case DS18B20_STEP_READ_DATA:
                    if (byte_idx < 9) {
                        ds18b20_set_input();
                        scratchpad[byte_idx] = ds18b20_read_byte();
                        byte_idx++;
                        return 0;
                    }
                    
                    /* Парсинг температуры */
                    int16_t raw_temp = (int16_t)(scratchpad[1] << 8) | scratchpad[0];
                    float temp_c = (float)raw_temp / 16.0f;
                    
                    current_reading.value = temp_c;
                    current_reading.raw = (uint16_t)raw_temp;
                    current_reading.valid = 1;
                    current_reading.error = SENSOR_OK;
                    current_reading.timestamp = HAL_GetTick() / 1000;
                    
                    sensor_history_add(&history, &current_reading);
                    
                    state = DS18B20_DONE;
                    return 1;
                    
                default:
                    break;
            }
            return 0;
            
        case DS18B20_DONE:
            return 1;
            
        default:
            state = DS18B20_IDLE;
            return -1;
    }
}

/* Получение результата */
int ds18b20_get(sensor_reading_t *out)
{
    if (!out) return -1;
    
    *out = current_reading;
    
    if (state == DS18B20_DONE) {
        state = DS18B20_IDLE;
    }
    
    return current_reading.valid ? SENSOR_OK : current_reading.error;
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
