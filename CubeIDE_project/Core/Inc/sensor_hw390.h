#ifndef SENSOR_HW390_H
#define SENSOR_HW390_H

#include <stdbool.h>
#include "sensor_common.h"

/* Значения калибровки по умолчанию (будут изменяемы в runtime) */
#define HW390_RAW_DRY_DEFAULT  3000  /* ADC значение для сухой почвы */
#define HW390_RAW_WET_DEFAULT  1500  /* ADC значение для влажной почвы (ниже ADC = влажнее) */

/* Error values */
#define HW390_ERROR_VALUE -500.0f

/* State machine timeouts */
#define HW390_POWER_ON_DELAY_MS 50
#define HW390_MEASURING_TIMEOUT_MS 2000

/* State machine states */
typedef enum {
    HW390_STATE_IDLE,
    HW390_STATE_POWER_ON_DELAY,
    HW390_STATE_MEASURING,
    HW390_STATE_READ,
    HW390_STATE_ERROR
} hw390_state_t;

/* State machine variables (extern for logging) */
extern volatile hw390_state_t hw390_state;
extern volatile uint32_t hw390_state_start_ms;
extern volatile bool hw390_busy;
extern volatile bool hw390_result_ready;

/* Внешние переменные калибровки (для обновления в runtime) */
extern uint16_t hw390_raw_dry;
extern uint16_t hw390_raw_wet;

/* API датчика */
int hw390_init(void);

/* Request measurement - non-blocking */
int hw390_request_measurement(void);

/* Get last result - non-blocking */
int hw390_get_result(sensor_reading_t *out);

/* State machine tick - call every main loop iteration */
void hw390_tick(void);

/* История */
int hw390_get_history(sensor_history_t *out);
void hw390_clear_history(void);

/* Получение/установка калибровки */
uint16_t hw390_get_raw_dry(void);
uint16_t hw390_get_raw_wet(void);
void hw390_set_calibration(uint16_t dry, uint16_t wet);

#endif /* SENSOR_HW390_H */
