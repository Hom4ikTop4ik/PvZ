#include "lora_driver.h"
#include "usart.h"
#include "gpio.h"
#include <string.h>

/* ============================================================================
 * Low-Level LoRa Driver Implementation
 * ============================================================================
 * Внешний дескриптор UART (LPUART1) - инициализируется в usart.c
 * ============================================================================ */
extern UART_HandleTypeDef hlpuart1;

/* AT mode sequence для E22 (время в миллисекундах) */
#define AT_MODE_ENTER_DELAY_MS  40
#define AT_MODE_EXIT_DELAY_MS   40

/* Инициализация */
lora_result_t lora_driver_init(void)
{
    /* LPUART1 уже инициализирован в MX_LPUART1_UART_Init */
    if (hlpuart1.gState == HAL_UART_STATE_RESET) {
        return LORA_ERR_INIT;
    }
    
    /* Выход из AT режима на случай если застряли */
    lora_driver_exit_at_mode();
    
    return LORA_OK;
}

/* ============================================================================
 * AUX Pin Handling - проверка готовности модуля E22
 * ============================================================================
 * E22: AUX=1 (высокий) - модуль готов к приему/передаче
 *       AUX=0 (низкий) - модуль занят (инициализация, передача)
 * ============================================================================ */
bool lora_driver_ready(void)
{
    return (HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN) == GPIO_PIN_SET);
}

/* Ожидание готовности с таймаутом */
bool lora_driver_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    
    while (!lora_driver_ready()) {
        if (HAL_GetTick() - start > timeout_ms) {
            return false;  /* таймаут */
        }
    }
    
    return true;
}

/* Отправка данных с проверкой AUX */
lora_result_t lora_driver_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        return LORA_ERR_SEND;
    }
    
    /* Проверка что модуль готов */
    if (!lora_driver_ready()) {
        /* Ждем готовности с коротким таймаутом */
        if (!lora_driver_wait_ready(100)) {
            return LORA_ERR_AUX_BUSY;
        }
    }
    
    /* Отправка через UART (блокирующая, но с коротким таймаутом) */
    HAL_StatusTypeDef status = HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, len, LORA_UART_TIMEOUT_MS);
    
    return (status == HAL_OK) ? LORA_OK : LORA_ERR_SEND;
}

/* Проверка наличия данных в RX */
bool lora_driver_data_available(void)
{
    return (__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_RXNE) != RESET);
}

/* Чтение одного байта (non-blocking) */
bool lora_driver_read_byte(uint8_t *byte)
{
    if (!byte) return false;
    
    if (lora_driver_data_available()) {
        *byte = (uint8_t)(hlpuart1.Instance->RDR & 0xFF);
        return true;
    }
    
    return false;
}

/* ============================================================================
 * Read Line with Character Timeout (не блокирует на 5 секунд!)
 * ============================================================================
 * Использует таймаут между символами (LORA_CHAR_TIMEOUT_MS).
 * Если между символами прошло больше таймаута - строка считается завершенной.
 * Максимальное время чтения ограничено LORA_LINE_MAX_TIMEOUT_MS.
 * ============================================================================ */
bool lora_driver_read_line(uint8_t *buffer, uint16_t max_len, uint16_t *out_len)
{
    if (!buffer || max_len == 0 || !out_len) return false;
    
    uint16_t idx = 0;
    uint32_t last_char_time = HAL_GetTick();
    uint32_t start_time = HAL_GetTick();
    bool has_content = false;
    
    while (1) {
        uint32_t now = HAL_GetTick();
        
        /* Общий таймаут на строку */
        if (now - start_time > LORA_LINE_MAX_TIMEOUT_MS) {
            if (has_content) {
                /* Возвращаем что успели прочитать */
                buffer[idx] = '\0';
                *out_len = idx;
                return true;
            }
            return false;
        }
        
        uint8_t byte;
        if (lora_driver_read_byte(&byte)) {
            has_content = true;
            last_char_time = now;
            
            /* Конец строки: CR или LF */
            if (byte == '\r' || byte == '\n') {
                if (idx > 0) {
                    buffer[idx] = '\0';
                    *out_len = idx;
                    return true;
                }
                /* Пропускаем пустые строки (CRLF) */
                continue;
            }
            
            /* Сохраняем байт если есть место */
            if (idx < max_len - 1) {
                buffer[idx++] = byte;
            }
            /* Если буфер переполнен - продолжаем читать до CR/LF */
        }
        else {
            /* Нет данных - проверяем таймаут между символами */
            if (has_content && (now - last_char_time > LORA_CHAR_TIMEOUT_MS)) {
                /* Таймаут между символами - считаем строку завершенной */
                buffer[idx] = '\0';
                *out_len = idx;
                return true;
            }
        }
    }
}

/* ============================================================================
 * AT Command Mode
 * ============================================================================
 * Вход: последовательность +++ с задержками
 * Выход: команда AT+EXIT
 * ============================================================================ */
lora_result_t lora_driver_enter_at_mode(void)
{
    uint8_t plus[] = "+++";
    
    HAL_Delay(AT_MODE_ENTER_DELAY_MS);
    HAL_UART_Transmit(&hlpuart1, plus, 3, 100);
    HAL_Delay(AT_MODE_ENTER_DELAY_MS);
    
    return LORA_OK;
}

lora_result_t lora_driver_exit_at_mode(void)
{
    uint8_t exit_cmd[] = "AT+EXIT\r\n";
    
    HAL_UART_Transmit(&hlpuart1, exit_cmd, sizeof(exit_cmd) - 1, 100);
    HAL_Delay(AT_MODE_EXIT_DELAY_MS);
    
    return LORA_OK;
}

/* ============================================================================
 * AT Commands - БАЗОВАЯ РЕАЛИЗАЦИЯ
 * ============================================================================
 * TODO: Текущая реализация читает только ОДНУ строку ответа.
 * 
 * Проблема: E22 может отвечать несколькими строками:
 *   - Обычный ответ: "OK\r\n" 
 *   - Ответ с данными: "+DATA\r\nOK\r\n"
 *   - Ошибка: "ERR\r\n"
 * 
 * Для production нужен multi-line parser, который:
 *   1. Читает строки до пустой строки или OK/ERR
 *   2. Сохраняет все строки в буфер
 *   3. Парсит результаты
 * 
 * Для первых тестов LoRa модуль уже настроен из ESP32 проекта.
 * Перенастройка через AT не требуется - используем существующую конфигурацию.
 * 
 * Если потребуется переконфигурировать модуль - расширить эту функцию
 * для чтения multi-line ответов.
 * ============================================================================ */
lora_result_t lora_driver_at_command(const char *cmd, char *response, uint16_t resp_len)
{
    if (!cmd) return LORA_ERR_SEND;
    
    /* Отправка команды */
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)cmd, strlen(cmd), 500);
    
    /* CR+LF */
    uint8_t crlf[] = "\r\n";
    HAL_UART_Transmit(&hlpuart1, crlf, 2, 100);
    
    /* Чтение ответа (только одна строка - см. TODO выше) */
    if (response && resp_len > 0) {
        uint16_t len;
        if (!lora_driver_read_line((uint8_t *)response, resp_len, &len)) {
            return LORA_ERR_TIMEOUT;
        }
    }
    
    return LORA_OK;
}
