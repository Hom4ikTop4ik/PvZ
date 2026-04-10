#ifndef LORA_DRIVER_H
#define LORA_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32l4xx_hal.h"
#include "lora_config.h"

/* ============================================================================
 * Low-Level LoRa Driver for EBYTE E22 400T30D
 * ============================================================================
 * Работает через UART (LPUART1), поддерживает проверку готовности через AUX.
 * Non-blocking API для интеграции в main loop.
 * ============================================================================ */

/* Результаты операций */
typedef enum {
    LORA_OK = 0,
    LORA_ERR_INIT,
    LORA_ERR_TIMEOUT,
    LORA_ERR_SEND,
    LORA_ERR_RECEIVE,
    LORA_ERR_AUX_BUSY      /* модуль занят (AUX=0) */
} lora_result_t;

/* Инициализация модуля E22 */
lora_result_t lora_driver_init(void);

/* Проверка готовности модуля через AUX пин */
bool lora_driver_ready(void);

/* Ожидание готовности модуля (с таймаутом) */
bool lora_driver_wait_ready(uint32_t timeout_ms);

/* Отправка данных (проверяет AUX перед отправкой) */
lora_result_t lora_driver_send(const uint8_t *data, uint16_t len);

/* Проверка наличия данных для чтения в UART */
bool lora_driver_data_available(void);

/* Чтение байта (non-blocking) */
bool lora_driver_read_byte(uint8_t *byte);

/* Чтение строки до CR/LF с таймаутом по символам */
bool lora_driver_read_line(uint8_t *buffer, uint16_t max_len, uint16_t *out_len);

/* Enter/exit AT command mode */
lora_result_t lora_driver_enter_at_mode(void);
lora_result_t lora_driver_exit_at_mode(void);

/* AT команды - базовая реализация (читает только одну строку ответа) */
/* TODO: E22 может отвечать несколькими строками. Для production нужен
 * multi-line parser. Для первых тестов модуль уже настроен из ESP32 проекта.
 */
lora_result_t lora_driver_at_command(const char *cmd, char *response, uint16_t resp_len);

#endif /* LORA_DRIVER_H */
