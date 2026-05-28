#ifndef SENSOR_DS18B20_H
#define SENSOR_DS18B20_H

#include "sensor_common.h"

/* Команды OneWire */
#define DS18B20_CMD_SKIP_ROM     0xCC
#define DS18B20_CMD_CONVERT_T    0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

/* Время конвертации для 9-bit разрешения (самое быстрое) */
#define DS18B20_CONV_TIME_MS     120

/* Заглушки power gating */
void ds18b20_power_on(void);
void ds18b20_power_off(void);

/* API датчика */
int ds18b20_init(void);
int ds18b20_start(void);
int ds18b20_poll(void);
int ds18b20_get(sensor_reading_t *out);

/* История */
int ds18b20_get_history(sensor_history_t *out);
void ds18b20_clear_history(void);

#endif /* SENSOR_DS18B20_H */
