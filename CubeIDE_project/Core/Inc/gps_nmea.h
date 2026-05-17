#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>

extern uint16_t gps_dma_tail;

/* Error values */
#define GPS_ERROR_LAT 0.0f
#define GPS_ERROR_LON 0.0f

/* State machine timeouts */
#define GPS_POWER_ON_DELAY_MS 100
#define GPS_GEO_INIT_DELAY_MS 500
#define GPS_COLLECT_TIMEOUT_MS (10*60*1000)  /* 10 minutes */

/* GPS quality thresholds */
#define GPS_MIN_QUALITY 1
#define GPS_MIN_SATELLITES 4
#define GPS_MAX_HDOP 2.0f
#define GPS_NUM_COORDINATES_TO_COLLECT 5
#define GPS_FIX_TIMEOUT_MS (10*60*1000)  /* 10 minutes */

/* GPS fix structure with quality metrics */
typedef struct {
    bool has_time;
    bool has_date;
    bool has_lat;
    bool has_lon;
    bool has_quality;      /* Quality indicator available */
    bool has_num_sats;      /* Number of satellites available */
    bool has_hdop;          /* HDOP available */
    char time[9];
    char date[11];
    float lat;
    float lon;
    uint8_t quality;        /* 0=invalid, 1=GPS, 2=DGPS */
    uint8_t num_sats;        /* Number of satellites */
    float hdop;             /* Horizontal dilution of precision */
} gps_fix_t;

/* State machine states */
typedef enum {
    GPS_STATE_IDLE = 0,
    GPS_STATE_POWER_ON,
    GPS_STATE_WAIT_POWER_ON,
    GPS_STATE_COLLECTING,
    GPS_STATE_CALC_BEST,
    GPS_STATE_READ,
    GPS_STATE_POWER_OFF,
    GPS_STATE_ERROR
} gps_state_t;

/* State machine variables (extern for logging) */
extern gps_state_t gps_state;
extern uint32_t gps_state_start_ms;
extern bool gps_busy;
extern bool gps_result_ready;

extern UART_HandleTypeDef huart1;

/* Current GPS fix (global for tick function access) */
extern gps_fix_t gps_current_fix;

/* Request measurement - non-blocking */
int gps_request_measurement(void);

/* Get last result - non-blocking */
int gps_get_result(float *lat, float *lon);

/* State machine tick - call every main loop iteration */
void gps_tick(void);

void gps_test_init(UART_HandleTypeDef *gps_uart);
void gps_test_poll(UART_HandleTypeDef *gps_uart);
void gps_poll_and_dump_all(UART_HandleTypeDef *gps_uart);
void gps_poll_anall(UART_HandleTypeDef *gps_uart);

#endif /* GPS_NMEA_H */
