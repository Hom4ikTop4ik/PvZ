/*
 * interval.c
 */

#include "printf.h"
#include "interval.h"
#include "interval_flash.h"
#include <stdio.h>

// Значения по умолчанию (секунды)
#define DEFAULT_MIN_PERIOD 15

#define DEFAULT_HUM_PERIOD 30
#define DEFAULT_TMP_PERIOD 30
#define DEFAULT_GEO_PERIOD 300  // 5 минут
#define DEFAULT_STT_PERIOD 120  // 2 минуты

uint32_t hum_period_s = DEFAULT_HUM_PERIOD;
uint32_t tmp_period_s = DEFAULT_TMP_PERIOD;
uint32_t gps_period_s = DEFAULT_GEO_PERIOD;
uint32_t stt_period_s = DEFAULT_STT_PERIOD;

volatile bool system_sleep_mode = false;

void interval_init(void)
{
    interval_config_t cfg;
    if (interval_flash_load(&cfg) == 0) {
        // Успешно загружено из FLASH
        hum_period_s = cfg.hum_interval;
        tmp_period_s = cfg.tmp_interval;
        gps_period_s = cfg.gps_interval;
        stt_period_s = cfg.stt_interval;
        PRINTF("[INTERVAL] Loaded from FLASH: hum=%lu tmp=%lu gps=%lu stt=%lu\r\n",
               hum_period_s, tmp_period_s, gps_period_s, stt_period_s);
    } else {
        // FLASH не инициализирован, сохраним текущие значения по умолчанию
        interval_config_t default_cfg = {
            .magic = INTERVAL_MAGIC,
            .hum_interval = DEFAULT_HUM_PERIOD,
            .tmp_interval = DEFAULT_TMP_PERIOD,
            .gps_interval = DEFAULT_GEO_PERIOD,
            .stt_interval = DEFAULT_STT_PERIOD
        };
        interval_flash_save(&default_cfg);
        PRINTF("[INTERVAL] Using default intervals\r\n");
    }
}

static void interval_save_all(void)
{
    interval_config_t cfg = {
        .magic = INTERVAL_MAGIC,
        .hum_interval = hum_period_s,
        .tmp_interval = tmp_period_s,
        .gps_interval = gps_period_s,
        .stt_interval = stt_period_s
    };
    interval_flash_save(&cfg);
}

void interval_set_hum(uint32_t seconds)
{
    if (seconds == 0) seconds = DEFAULT_MIN_PERIOD; // защита от нуля
    hum_period_s = seconds;
    interval_save_all();
    PRINTF("[INTERVAL] Humidity period set to %lu seconds\r\n", seconds);
}

void interval_set_tmp(uint32_t seconds)
{
    if (seconds == 0) seconds = DEFAULT_MIN_PERIOD;
    tmp_period_s = seconds;
    interval_save_all();
    PRINTF("[INTERVAL] Temperature period set to %lu seconds\r\n", seconds);
}

void interval_set_gps(uint32_t seconds)
{
    if (seconds == 0) seconds = DEFAULT_MIN_PERIOD;
    gps_period_s = seconds;
    interval_save_all();
    PRINTF("[INTERVAL] GPS period set to %lu seconds\r\n", seconds);
}

void interval_set_stt(uint32_t seconds)
{
    if (seconds == 0) seconds = DEFAULT_MIN_PERIOD;
    stt_period_s = seconds;
    interval_save_all();
    PRINTF("[INTERVAL] Status period set to %lu seconds\r\n", seconds);
}
