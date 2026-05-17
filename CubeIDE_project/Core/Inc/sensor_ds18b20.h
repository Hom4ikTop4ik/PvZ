#ifndef SENSOR_DS18B20_H
#define SENSOR_DS18B20_H

#include <stdbool.h>
#include "sensor_common.h"

/* Команды OneWire */
#define DS18B20_CMD_SKIP_ROM     0xCC
#define DS18B20_CMD_CONVERT_T    0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

/* Время конвертации для 9-bit разрешения (самое быстрое) */
#define DS18B20_CONV_TIME_MS     120

/* Error values */
#define DS18B20_ERROR_VALUE -500.0f

/* State machine timeout */
#define DS18B20_CONVERSION_TIMEOUT_MS 1000

/* State machine states */
typedef enum {
    DS18B20_STATE_IDLE,
    DS18B20_STATE_START_CONVERSION,
    DS18B20_STATE_WAIT_CONVERSION,
    DS18B20_STATE_READ_SCRATCHPAD,
    DS18B20_STATE_ERROR
} ds18b20_state_t;

/* State machine variables (extern for logging) */
extern volatile ds18b20_state_t ds18b20_state;
extern volatile uint32_t ds18b20_state_start_ms;
extern volatile bool ds18b20_busy;
extern volatile bool ds18b20_result_ready;

/* API датчика */
int ds18b20_init(void);

/* Request measurement - non-blocking */
int ds18b20_request_measurement(void);

/* Get last result - non-blocking */
int ds18b20_get_result(sensor_reading_t *out);

/* State machine tick - call every main loop iteration */
void ds18b20_tick(void);

/* История */
int ds18b20_get_history(sensor_history_t *out);
void ds18b20_clear_history(void);

#endif /* SENSOR_DS18B20_H */
