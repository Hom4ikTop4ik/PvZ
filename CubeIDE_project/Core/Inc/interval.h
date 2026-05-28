/*
 * interval.h
 */

#ifndef INTERVAL_H
#define INTERVAL_H

#include <stdint.h>
#include <stdbool.h>

// Глобальные переменные для периодов измерений (в секундах)
extern uint32_t hum_period_s;
extern uint32_t tmp_period_s;
extern uint32_t gps_period_s;
extern uint32_t stt_period_s;

// Флаг сна (пока просто блокирует отправку, TODO – глубокий сон)
extern volatile bool system_sleep_mode;

// Инициализация: загружает интервалы из FLASH или устанавливает значения по умолчанию
void interval_init(void);

// Обновление интервала с сохранением во FLASH
void interval_set_hum(uint32_t seconds);
void interval_set_tmp(uint32_t seconds);
void interval_set_gps(uint32_t seconds);
void interval_set_stt(uint32_t seconds);

#endif /* INTERVAL_H */
