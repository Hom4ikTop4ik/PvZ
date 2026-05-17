#include "hw390_flash.h"
#include "stm32l4xx_hal.h"    // или ваш HAL

// TODO
// ПРОВЕРИТЬ АДРЕС
#define HW390_FLASH_ADDR  ((uint32_t)0x0803F800) // Пример! Подберите под свою MCU (обычно последние 2кб)

#define HW390_MAGIC 0x39393048 // 'H090' — маркер для вашей структуры

static uint32_t hw390_flash_addr_to_page(uint32_t addr) {
    return (addr - FLASH_BASE) / FLASH_PAGE_SIZE;
}

int hw390_flash_save_calibration(uint16_t dry, uint16_t wet) {
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
	    .Page = hw390_flash_addr_to_page(HW390_FLASH_ADDR),
        .NbPages = 1
    };
    uint32_t page_err = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_err) != HAL_OK) return -1;

    hw390_calib_t data = {HW390_MAGIC, dry, wet};

    // Записываем 8 байт: структуру как uint64_t
    uint64_t *pdata = (uint64_t*)&data;
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, HW390_FLASH_ADDR, *pdata) != HAL_OK) {
            HAL_FLASH_Lock();
            return -2;
    }

    HAL_FLASH_Lock();
    return 0;
}

int hw390_flash_load_calibration(uint16_t *dry, uint16_t *wet) {
    hw390_calib_t *pdata = (hw390_calib_t *)HW390_FLASH_ADDR;
    if (pdata->magic == HW390_MAGIC) {
        if (dry) *dry = pdata->dry;
        if (wet) *wet = pdata->wet;
        return 0;
    }
    return -1;
}
