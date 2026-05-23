/*
 * gps_time.h
 *
 */

#ifndef GPS_TIME_H
#define GPS_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32l4xx_hal.h"   // или свой HAL

// Время ожидания GPS времени (ms)
#define GPS_TIME_SYNC_TIMEOUT_MS ((uint32_t)30*60*1000) // 30 минут (на запрос времени от спутников)

// Глобальный флаг
extern volatile bool gps_time_synchronized;

// Инициализация модуля (подключение парсера, callback и т.д.)
void gps_time_init(void);

// Запустить синхронизацию времени по GPS (блокирующая)
// Возвращает true, если время было успешно получено и установлено в RTC, иначе false.
bool gps_time_sync_blocking(RTC_HandleTypeDef *hrtc);

// Парсер GPRMC (или GPZDA) строк (вызывать при каждом поступлении строки из GPS)
void gps_time_process_gprmc(const char *gprmc_string, RTC_HandleTypeDef *hrtc);

#endif // GPS_TIME_H
