#include "gps_nmea.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPS_TEST_BAUD_RATE 9600U
#define GPS_LINE_MAX 128U
#define GPS_DMA_RX_SIZE 512U
#define GPS_POLL_BUDGET_BYTES 96U
#define GPS_STATUS_INTERVAL_MS 10000U
#define GPS_LEGACY_UBX_CONFIG 1

typedef struct {
    bool has_time;
    bool has_date;
    bool has_lat;
    bool has_lon;
    char time[9];
    char date[11];
    float lat;
    float lon;
} gps_fix_t;

static char gps_line[GPS_LINE_MAX];
static uint16_t gps_line_len;
static uint8_t gps_dma_rx[GPS_DMA_RX_SIZE];
static uint16_t gps_dma_tail;
static uint32_t gps_current_baud;
static bool gps_nmea_seen;
static bool gps_fix_seen;
static uint32_t gps_rx_bytes;
static uint32_t gps_last_status_ms;
static uint32_t gps_last_rx_bytes;
static uint32_t gps_uart_errors;
static uint32_t gps_last_uart_errors;
static uint32_t gps_last_uart_isr;

#if GPS_LEGACY_UBX_CONFIG
static const uint8_t payload_cfg_gnss[] = {
    0x00,       /* msgVer */
    0x20,       /* numTrkChHw */
    0x20,       /* numTrkChUse */
    0x04,       /* numConfigBlocks */

    0x00,       /* GPS */
    0x04,
    0xFF,
    0x00,
    0x00, 0x00, 0x00, 0x00,

    0x01,       /* SBAS */
    0x00,
    0x03,
    0x00,
    0x00, 0x00, 0x00, 0x00,

    0x05,       /* QZSS */
    0x00,
    0x03,
    0x00,
    0x00, 0x00, 0x00, 0x00,

    0x06,       /* GLONASS */
    0x08,
    0xFF,
    0x00,
    0x01, 0x00, 0x00, 0x00,
};

static const uint8_t payload_cfg_save[] = {
    0x00, 0x00, 0x00, 0x00,
    0x1F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x1F,
};

static const uint8_t payload_cfg_rst[] = {
    0xFF, 0xFF, 0x01, 0x00,
};

static void checksum_update(uint8_t *ck_a, uint8_t *ck_b, uint8_t value)
{
    *ck_a = (uint8_t)(*ck_a + value);
    *ck_b = (uint8_t)(*ck_b + *ck_a);
}

static void send_ubx(UART_HandleTypeDef *uart, uint8_t msg_class, uint8_t msg_id,
                     const uint8_t *payload, uint16_t payload_len)
{
    uint8_t header[6] = {
        0xB5, 0x62, msg_class, msg_id,
        (uint8_t)(payload_len & 0xFFU),
        (uint8_t)(payload_len >> 8),
    };
    uint8_t checksum[2] = {0, 0};

    for (uint16_t i = 2U; i < sizeof(header); ++i) {
        checksum_update(&checksum[0], &checksum[1], header[i]);
    }
    for (uint16_t i = 0U; i < payload_len; ++i) {
        checksum_update(&checksum[0], &checksum[1], payload[i]);
    }

    HAL_UART_Transmit(uart, header, sizeof(header), HAL_MAX_DELAY);
    if (payload_len > 0U) {
        HAL_UART_Transmit(uart, (uint8_t *)payload, payload_len, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(uart, checksum, sizeof(checksum), HAL_MAX_DELAY);

    printf("-> UBX Class 0x%02X ID 0x%02X sent\r\n", msg_class, msg_id);
}
#endif

static void gps_flush_rx(UART_HandleTypeDef *uart)
{
    uint8_t byte;

    for (uint16_t i = 0; i < 512U; ++i) {
        if (HAL_UART_Receive(uart, &byte, 1U, 1U) != HAL_OK) {
            break;
        }
    }
}

static bool is_known_nmea_talker(const char *line)
{
    if (!line || strlen(line) < 6U || line[0] != '$') {
        return false;
    }

    return ((line[1] == 'G' && line[2] == 'P') ||
            (line[1] == 'G' && line[2] == 'N') ||
            (line[1] == 'G' && line[2] == 'L') ||
            (line[1] == 'G' && line[2] == 'A') ||
            (line[1] == 'G' && line[2] == 'B') ||
            (line[1] == 'G' && line[2] == 'Q'));
}

static bool nmea_coord_to_float(const char *coord, char direction, float *out)
{
    int deg_len;
    char deg_text[4] = {0};
    float minutes;
    float value;

    if (!coord || !coord[0] || !out) {
        return false;
    }

    if (direction == 'N' || direction == 'S') {
        deg_len = 2;
    } else if (direction == 'E' || direction == 'W') {
        deg_len = 3;
    } else {
        return false;
    }

    if ((int)strlen(coord) <= deg_len) {
        return false;
    }

    memcpy(deg_text, coord, (size_t)deg_len);
    minutes = strtof(coord + deg_len, NULL);
    value = (float)atoi(deg_text) + (minutes / 60.0f);

    if (direction == 'S' || direction == 'W') {
        value = -value;
    }

    *out = value;
    return true;
}

static bool parse_nmea_time(const char *text, char *out, size_t out_len)
{
    if (!text || strlen(text) < 6U || out_len < 9U) {
        return false;
    }

    snprintf(out, out_len, "%c%c:%c%c:%c%c",
             text[0], text[1], text[2], text[3], text[4], text[5]);
    return true;
}

static bool parse_nmea_date(const char *text, char *out, size_t out_len)
{
    if (!text || strlen(text) != 6U || out_len < 11U) {
        return false;
    }

    snprintf(out, out_len, "%c%c.%c%c.20%c%c",
             text[0], text[1], text[2], text[3], text[4], text[5]);
    return true;
}

static int split_csv(char *line, char *fields[], int max_fields)
{
    int count = 0;

    if (!line || max_fields <= 0) {
        return 0;
    }

    fields[count++] = line;
    for (char *p = line; *p != '\0' && count < max_fields; ++p) {
        if (*p == ',') {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }

    return count;
}

static bool sentence_type_is(const char *sentence, const char *type)
{
    size_t len;

    if (!sentence || !type) {
        return false;
    }

    len = strlen(sentence);
    return (len >= 6U && strcmp(sentence + len - 3U, type) == 0);
}

static bool parse_nmea_line(const char *line, gps_fix_t *fix)
{
    char work[GPS_LINE_MAX];
    char *fields[24];
    int field_count;

    if (!line || !fix || line[0] != '$') {
        return false;
    }

    memset(fix, 0, sizeof(*fix));
    strncpy(work, line, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    for (char *p = work; *p != '\0'; ++p) {
        if (*p == '*') {
            *p = '\0';
            break;
        }
    }

    field_count = split_csv(work, fields, (int)(sizeof(fields) / sizeof(fields[0])));
    if (field_count == 0) {
        return false;
    }

    if (sentence_type_is(fields[0], "RMC") && field_count >= 10) {
        fix->has_time = parse_nmea_time(fields[1], fix->time, sizeof(fix->time));
        fix->has_lat = nmea_coord_to_float(fields[3], fields[4][0], &fix->lat);
        fix->has_lon = nmea_coord_to_float(fields[5], fields[6][0], &fix->lon);
        fix->has_date = parse_nmea_date(fields[9], fix->date, sizeof(fix->date));
        return fix->has_time || fix->has_date || fix->has_lat || fix->has_lon;
    }

    if (sentence_type_is(fields[0], "GGA") && field_count >= 6) {
        fix->has_time = parse_nmea_time(fields[1], fix->time, sizeof(fix->time));
        fix->has_lat = nmea_coord_to_float(fields[2], fields[3][0], &fix->lat);
        fix->has_lon = nmea_coord_to_float(fields[4], fields[5][0], &fix->lon);
        return fix->has_time || fix->has_lat || fix->has_lon;
    }

    return false;
}

static void print_fix(const gps_fix_t *fix)
{
    printf("TIME: %s DATE: %s LAT: ",
           fix->has_time ? fix->time : "-",
           fix->has_date ? fix->date : "-");

    if (fix->has_lat) {
        printf("%.6f", fix->lat);
    } else {
        printf("-");
    }

    printf(" LON: ");
    if (fix->has_lon) {
        printf("%.6f", fix->lon);
    } else {
        printf("-");
    }

    printf("\r\n");
}

static void process_line(const char *line)
{
    gps_fix_t fix;

    if (!line || line[0] == '\0') {
        return;
    }

    if (!is_known_nmea_talker(line)) {
        return;
    }

    if (!gps_nmea_seen) {
        gps_nmea_seen = true;
        printf("GPS NMEA detected at %lu baud\r\n", (unsigned long)gps_current_baud);
    }

    if (parse_nmea_line(line, &fix)) {
        print_fix(&fix);
        if (fix.has_lat && fix.has_lon) {
            if (!gps_fix_seen) {
                gps_fix_seen = true;
                printf("GPS fix acquired\r\n");
            }
            printf("COORDS: %.6f %.6f\r\n", fix.lat, fix.lon);
        }
    }

    printf("%s\r\n", line);
}

static void push_rx_byte(uint8_t byte)
{
    if (byte == '\r' || byte == '\n') {
        if (gps_line_len > 0U) {
            gps_line[gps_line_len] = '\0';
            process_line(gps_line);
            gps_line_len = 0U;
        }
        return;
    }

    if (byte < 0x20U || byte > 0x7EU) {
        return;
    }

    if (gps_line_len < (GPS_LINE_MAX - 1U)) {
        gps_line[gps_line_len++] = (char)byte;
    } else {
        gps_line_len = 0U;
    }
}

static void gps_start_rx_dma(UART_HandleTypeDef *gps_uart)
{
    gps_dma_tail = 0U;
    memset(gps_dma_rx, 0, sizeof(gps_dma_rx));

    if (gps_uart->hdmarx != NULL) {
        HAL_UART_Receive_DMA(gps_uart, gps_dma_rx, sizeof(gps_dma_rx));
        printf("GPS DMA RX enabled (%u bytes)\r\n", (unsigned)sizeof(gps_dma_rx));
    } else {
        printf("GPS DMA RX is not linked, using register polling\r\n");
    }
}

void gps_test_init(UART_HandleTypeDef *gps_uart)
{
    gps_line_len = 0U;
    gps_current_baud = GPS_TEST_BAUD_RATE;
    gps_nmea_seen = false;
    gps_fix_seen = false;
    gps_rx_bytes = 0U;
    gps_last_rx_bytes = 0U;
    gps_uart_errors = 0U;
    gps_last_uart_errors = 0U;
    gps_last_uart_isr = 0U;
    gps_last_status_ms = HAL_GetTick();

    printf("GPS-only fixed listener: USART1 PA9=TX, PA10=RX, baud=%lu\r\n",
           (unsigned long)gps_current_baud);
    printf("GPS USART1 RX inversion is disabled\r\n");
    gps_flush_rx(gps_uart);
#if GPS_LEGACY_UBX_CONFIG
    printf("Applying legacy GNSS config: GPS/SBAS/QZSS off, GLONASS on\r\n");
    send_ubx(gps_uart, 0x06, 0x3E, payload_cfg_gnss, sizeof(payload_cfg_gnss));
    HAL_Delay(1000);

    printf("Saving GNSS config\r\n");
    send_ubx(gps_uart, 0x06, 0x09, payload_cfg_save, sizeof(payload_cfg_save));
    HAL_Delay(1000);

    printf("Cold-starting GPS module\r\n");
    send_ubx(gps_uart, 0x06, 0x04, payload_cfg_rst, sizeof(payload_cfg_rst));
    printf("Waiting after cold start (5 sec)\r\n");
    HAL_Delay(5000);
#endif
    gps_flush_rx(gps_uart);
    gps_start_rx_dma(gps_uart);
    printf("--- GPS NMEA readout ---\r\n");
}

void gps_test_poll(UART_HandleTypeDef *gps_uart)
{
    uint16_t processed = 0U;
    uint32_t now = HAL_GetTick();
    uint32_t isr = gps_uart->Instance->ISR;
    uint32_t error_flags = isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE);

    if (error_flags != 0U) {
        gps_uart_errors++;
        gps_last_uart_isr = isr;
    }

    if (gps_uart->hdmarx != NULL && gps_uart->hdmarx->Instance != NULL) {
        uint16_t head = (uint16_t)((GPS_DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(gps_uart->hdmarx)) % GPS_DMA_RX_SIZE);

        while (gps_dma_tail != head && processed < GPS_POLL_BUDGET_BYTES) {
            uint8_t byte = gps_dma_rx[gps_dma_tail];
            gps_dma_tail = (uint16_t)((gps_dma_tail + 1U) % GPS_DMA_RX_SIZE);
            gps_rx_bytes++;
            push_rx_byte(byte);
            ++processed;
        }
    } else {
        while (__HAL_UART_GET_FLAG(gps_uart, UART_FLAG_RXNE) != RESET &&
               processed < GPS_POLL_BUDGET_BYTES) {
            uint8_t byte = (uint8_t)(gps_uart->Instance->RDR & 0xFFU);
            gps_rx_bytes++;
            push_rx_byte(byte);
            ++processed;
        }
    }

    if (error_flags != 0U) {
        __HAL_UART_CLEAR_OREFLAG(gps_uart);
        __HAL_UART_CLEAR_FEFLAG(gps_uart);
        __HAL_UART_CLEAR_NEFLAG(gps_uart);
        __HAL_UART_CLEAR_PEFLAG(gps_uart);
    }

    if ((now - gps_last_status_ms) >= GPS_STATUS_INTERVAL_MS) {
        uint32_t delta = gps_rx_bytes - gps_last_rx_bytes;
        uint32_t err_delta = gps_uart_errors - gps_last_uart_errors;
        if (!gps_nmea_seen) {
            printf("GPS waiting for fix: rx+%lu total=%lu err+%lu\r\n",
                   (unsigned long)delta,
                   (unsigned long)gps_rx_bytes,
                   (unsigned long)err_delta);
        } else if (err_delta > 0U || delta == 0U) {
            printf("GPS status: rx+%lu total=%lu err+%lu lastISR=0x%08lX\r\n",
                   (unsigned long)delta,
                   (unsigned long)gps_rx_bytes,
                   (unsigned long)err_delta,
                   (unsigned long)gps_last_uart_isr);
        }
        if (err_delta > 0U && !gps_fix_seen) {
            printf("GPS UART errors: err+%lu lastISR=0x%08lX\r\n",
                   (unsigned long)err_delta,
                   (unsigned long)gps_last_uart_isr);
        }
        gps_last_rx_bytes = gps_rx_bytes;
        gps_last_uart_errors = gps_uart_errors;
        gps_last_status_ms = now;
    }
}
