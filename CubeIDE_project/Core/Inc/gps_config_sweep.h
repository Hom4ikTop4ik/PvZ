#ifndef GPS_CONFIG_SWEEP_H
#define GPS_CONFIG_SWEEP_H

#include "stm32l4xx_hal.h"

/*
 * Automatic GPS/GLONASS startup tester.
 *
 * It tries CFG-GNSS payload variants one by one. If a variant returns UBX ACK,
 * the sweep stops immediately; only the accepted variant is saved/reset.
 * If all ACK-checked variants fail, the legacy ESP32/Python sequence can be
 * sent as a last fallback.
 */
#ifndef GPS_CONFIG_SAVE_ACCEPTED
#define GPS_CONFIG_SAVE_ACCEPTED 1
#endif

#ifndef GPS_CONFIG_COLD_START_ACCEPTED
#define GPS_CONFIG_COLD_START_ACCEPTED 1
#endif

#ifndef GPS_CONFIG_LEGACY_FALLBACK_IF_ALL_FAIL
#define GPS_CONFIG_LEGACY_FALLBACK_IF_ALL_FAIL 1
#endif

void gps_config_sweep_run(UART_HandleTypeDef *gps_uart);

#endif /* GPS_CONFIG_SWEEP_H */
