#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include "stm32l4xx_hal.h"

void gps_test_init(UART_HandleTypeDef *gps_uart);
void gps_test_poll(UART_HandleTypeDef *gps_uart);

#endif /* GPS_NMEA_H */
