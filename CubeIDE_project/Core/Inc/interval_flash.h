/*
 * interval_flash.h
 */
#ifndef INTERVAL_FLASH_H
#define INTERVAL_FLASH_H

#include <stdint.h>

#define INTERVAL_FLASH_ADDR  ((uint32_t)0x0803F000) // рядом с калибровкой HW390
#define INTERVAL_MAGIC       0x494E54455256414CULL  // 'INTERVAL' в ASCII (8 байт)

typedef struct {
    uint64_t magic; // 8 байт, doubleword
    uint32_t hum_interval;
    uint32_t tmp_interval;
    uint32_t gps_interval;
    uint32_t stt_interval;
} interval_config_t;

// Сохранить конфигурацию во FLASH
int interval_flash_save(const interval_config_t *cfg);
// Загрузить из FLASH, вернуть 0 при успехе
int interval_flash_load(interval_config_t *cfg);

#endif /* INTERVAL_FLASH_H */
