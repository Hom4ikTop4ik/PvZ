/*
 * hw390_flash.h
 */

#ifndef HW390_FLASH_H
#define HW390_FLASH_H

#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint16_t dry;
    uint16_t wet;
} hw390_calib_t;

int hw390_flash_save_calibration(uint16_t dry, uint16_t wet);
int hw390_flash_load_calibration(uint16_t *dry, uint16_t *wet);

#endif
