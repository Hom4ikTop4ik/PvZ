#ifndef SENSOR_HW390_H
#define SENSOR_HW390_H

#include "sensor_common.h"

/* Значения калибровки по умолчанию (будут изменяемы в runtime) */
#define HW390_RAW_DRY_DEFAULT  3000  /* ADC значение для сухой почвы */
#define HW390_RAW_WET_DEFAULT  1500  /* ADC значение для влажной почвы (ниже ADC = влажнее) */

/* Внешние переменные калибровки (для обновления в runtime) */
extern uint16_t hw390_raw_dry;
extern uint16_t hw390_raw_wet;

/* Заглушки power gating */
void hw390_power_on(void);
void hw390_power_off(void);

/* API датчика */
int hw390_init(void);
int hw390_start(void);
int hw390_poll(void);
int hw390_get(sensor_reading_t *out);

/* История */
int hw390_get_history(sensor_history_t *out);
void hw390_clear_history(void);

/* Получение/установка калибровки */
uint16_t hw390_get_raw_dry(void);
uint16_t hw390_get_raw_wet(void);
void hw390_set_calibration(uint16_t dry, uint16_t wet);

#endif /* SENSOR_HW390_H */
