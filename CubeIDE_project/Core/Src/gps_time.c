/*
 * gps_time.c
 *
 */

#include "gps_time.h"
#include "gps_nmea.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

volatile bool gps_time_synchronized = false;

// --- Парсер GPRMC строки ---
// GPRMC cодержит время UTC (hhmmss), дату (ddmmyy) и валидность (A/V)
// Пример: $GPRMC,142844.00,A,5549.79583,N,03739.05257,E,0.301,,260524,,,A*66
//         $GPRMC,<utc-time>,<valid>,...,,,<date(ddmmyy)>,,,
static bool parse_gprmc(const char *str, int *hh, int *mm, int *ss, int *D, int *M, int *Y)
{
    if (!str) return false;

    // Строго разобрать utc и дату (бывает что бывает только часть строки)
    // Ищем первое поле после $, потом 1е=тип, 2е=время, 3е=A/V, ..., 9е=дата
    // Разобрать поля через strtok (локально! не поточно!)
    char buf[128];
    strncpy(buf, str, sizeof(buf)-1); buf[sizeof(buf)-1]=0;

    char *fields[12] = {0};
    char *p = buf;
    int nf = 0;
    while ((fields[nf] = strsep(&p, ",")) && nf < 12) nf++;

    if (nf < 10) return false;
    // fields[1] = UTC time (hhmmss)
    // fields[2] = A or V (Active or Void)
    // fields[9] = date (ddmmyy)

    // валидность: только если "A"
    //if (fields[2][0] != 'A') return false;

    // время
    if (strlen(fields[1]) < 6) return false;
    char utc[7] = {0};
    strncpy(utc, fields[1], 6);
    *hh = (utc[0]-'0')*10 + (utc[1]-'0');
    *mm = (utc[2]-'0')*10 + (utc[3]-'0');
    *ss = (utc[4]-'0')*10 + (utc[5]-'0');

    // дата
    if (strlen(fields[9]) < 6) return false;
    char dat[7] = {0};
    strncpy(dat, fields[9], 6);
    *D = (dat[0]-'0')*10 + (dat[1]-'0');
    *M = (dat[2]-'0')*10 + (dat[3]-'0');
    *Y = (dat[4]-'0')*10 + (dat[5]-'0'); // 2-значный год

    return true;
}

// Сброс флага сихронизированности в false
void gps_time_init(void) {
    gps_time_synchronized = false;
}

// Call this (например из UART-прерывания или gps_tick), когда получена новая строка GPRMC
void gps_time_process_gprmc(const char *gprmc_string, RTC_HandleTypeDef *hrtc)
{
    int hh, mm, ss, D, M, Y;
    if (!hrtc) return;
    if (parse_gprmc(gprmc_string, &hh, &mm, &ss, &D, &M, &Y)) {
        RTC_TimeTypeDef sTime = {0};
        RTC_DateTypeDef sDate = {0};
        sTime.Hours = hh;
        sTime.Minutes = mm;
        sTime.Seconds = ss;
        sDate.Date = D;
        sDate.Month = M;
        sDate.Year = Y; // STM32: только младшие 2 цифры!
        if (HAL_RTC_SetTime(hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK &&
            HAL_RTC_SetDate(hrtc, &sDate, RTC_FORMAT_BIN) == HAL_OK) {
            gps_time_synchronized = true;
        }
    }
}

// Запускает синхронизацию времени по GPS (блокирующая)
bool gps_time_sync_blocking(RTC_HandleTypeDef *hrtc)
{
    gps_time_synchronized = false;
    uint32_t start = HAL_GetTick();

    gps_power_on();

    // Ожидание синхронизации времени с GPS
    while (!gps_time_synchronized && (HAL_GetTick() - start) < GPS_TIME_SYNC_TIMEOUT_MS) {
        // Активно опрашиваем ringbuffer/DMA и парсим новые строки
        gps_poll_anall(&huart1);
        HAL_Delay(1);
    }

    return gps_time_synchronized;
}
