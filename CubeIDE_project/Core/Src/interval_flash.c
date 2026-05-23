/*
 * interval_flash.c
 */

#include "interval_flash.h"
#include "stm32l4xx_hal.h"

static uint32_t interval_flash_addr_to_page(uint32_t addr)
{
    return (addr - FLASH_BASE) / FLASH_PAGE_SIZE;
}

int interval_flash_save(const interval_config_t *cfg)
{
    if (cfg == NULL) return -1;

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Page = interval_flash_addr_to_page(INTERVAL_FLASH_ADDR),
        .NbPages = 1
    };
    uint32_t page_err = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_err) != HAL_OK) {
        HAL_FLASH_Lock();
        return -2;
    }

    const uint64_t *pdata = (const uint64_t*)cfg;
    // Записываем три doubleword (размер структуры 24 байта)
    for (int i = 0; i < 3; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              INTERVAL_FLASH_ADDR + i * 8, pdata[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return -3 - i;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

int interval_flash_load(interval_config_t *cfg)
{
    if (cfg == NULL) return -1;
    interval_config_t *pdata = (interval_config_t*)INTERVAL_FLASH_ADDR;
    if (pdata->magic == INTERVAL_MAGIC) {
        *cfg = *pdata;
        return 0;
    }
    return -2;
}
