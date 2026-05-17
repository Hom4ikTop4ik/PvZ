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

/* ============================================================================
 * Config Mode and RSSI Reading
 * ============================================================================
 * M0/M1 control for mode switching and RSSI reading via register commands
 * ============================================================================ */

/* Set LoRa mode via M0/M1 pins */
/* Mode 0: Normal (M0=0, M1=0) */
/* Mode 1: Wake-up (M0=1, M1=0) */
/* Mode 2: Power-saving/Config (M0=0, M1=1) */
/* Mode 3: Sleep (M0=1, M1=1) */
void lora_driver_set_mode(uint8_t mode)
{
    GPIO_PinState m0_state = GPIO_PIN_RESET;
    GPIO_PinState m1_state = GPIO_PIN_RESET;

    switch (mode) {
        case 0: /* Normal mode */
            m0_state = GPIO_PIN_RESET;
            m1_state = GPIO_PIN_RESET;
            break;
        case 1: /* Wake-up mode */
            m0_state = GPIO_PIN_SET;
            m1_state = GPIO_PIN_RESET;
            break;
        case 2: /* Power-saving/Config mode */
            m0_state = GPIO_PIN_RESET;
            m1_state = GPIO_PIN_SET;
            break;
        case 3: /* Sleep mode */
            m0_state = GPIO_PIN_SET;
            m1_state = GPIO_PIN_SET;
            break;
        default:
            break;
    }

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, m0_state);  /* LORA_M0 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, m1_state);  /* LORA_M1 */

    /* Small delay for mode switch */
    HAL_Delay(10);
}

/* Read RSSI from LoRa module (requires config mode) */
/* ambient=true: read ambient noise RSSI (register 0x00) */
/* ambient=false: read last packet RSSI (register 0x01) */
/* Returns RSSI in dBm (negative value), or 0 on error */
int8_t lora_driver_read_rssi(bool ambient)
{
    uint8_t cmd[6] = {0xC0, 0xC1, 0xC2, 0xC3, 0x00, 0x01};  /* Read ambient RSSI */
    uint8_t response[8] = {0};
    uint16_t len = 0;

    if (!ambient) {
        cmd[4] = 0x01;  /* Read packet RSSI */
    }

    /* Switch to config mode (mode 2) */
    lora_driver_set_mode(2);

    /* Send RSSI read command */
    if (HAL_UART_Transmit(&hlpuart1, cmd, sizeof(cmd), 100) != HAL_OK) {
        lora_driver_set_mode(0);  /* Restore normal mode */
        return 0;
    }

    /* Read response (C1 + address + length + value) */
    /* Expected response: C1 00 01 <value> */
    if (!lora_driver_read_line(response, sizeof(response), &len)) {
        lora_driver_set_mode(0);  /* Restore normal mode */
        return 0;
    }

    /* Restore normal mode */
    lora_driver_set_mode(0);

    /* Parse response: C1 00 01 <value> */
    if (len >= 4 && response[0] == 0xC1 && response[1] == (ambient ? 0x00 : 0x01) && response[2] == 0x01) {
        uint8_t rssi_raw = response[3];
        /* dBm = rssi_raw / 2 (negative) */
        return -(int8_t)(rssi_raw / 2);
    }

    return 0;
}

/**
 * @brief  Считывает шум и RSSI пакета за один запрос. Вычисляет целое RSSI и дробное SNR.
 * @param  out_rssi_packet: Указатель на RSSI пакета в целых дБм (int16_t)
 * @param  out_snr: Указатель на SNR в дБ с плавающей точкой (float)
 * @return lora_result_t Код операции из системного перечисления
 */
lora_result_t lora_driver_read_rssi_and_snr(int16_t *out_rssi_packet, float *out_snr)
{
    /* Запрос: Чтение с адреса 0x00, длина 2 байта */
    uint8_t cmd[6] = {0xC0, 0xC1, 0xC2, 0xC3, 0x00, 0x02};
    uint8_t response[8] = {0};
    uint16_t len = 0;

    /* Переключаемся в режим конфигурации (Режим 2) */
    lora_driver_set_mode(2);

    /* Отправляем команду чтения пакета регистров */
    if (HAL_UART_Transmit(&hlpuart1, cmd, sizeof(cmd), 100) != HAL_OK) {
        lora_driver_set_mode(0); /* Возвращаем нормальный режим */
        return LORA_ERR_SEND;
    }

    /* Ожидаем ответ от модуля (C1 00 02 [Шум RSSI] [Пакет RSSI]) */
    if (!lora_driver_read_line(response, sizeof(response), &len)) {
        lora_driver_set_mode(0);
        return LORA_ERR_TIMEOUT; /* Используем ваш тайм-аут */
    }

    /* Возвращаем модуль в нормальный режим работы сразу после чтения */
    lora_driver_set_mode(0);

    /* Валидация ответа: заголовок + 2 байта данных */
    // Сырые данные умножены на -2 (raw_RSSI = 190 -> RSSI = -95 dBm)
    if (len >= 5 && response[0] == 0xC1 && response[1] == 0x00 && response[2] == 0x02) {
        uint8_t raw_noise  = response[3]; /* Регистр 0x00 */
        uint8_t raw_packet = response[4]; /* Регистр 0x01 */

        /* 
         * 1. Расчет RSSI сигнала в целых дБм с математическим округлением.
         * Формула -((raw + 1) / 2) корректно округляет полуцелые значения.
         */
        int16_t p_signal_int = -(((int16_t)raw_packet + 1) / 2);

        /* Записываем результаты по указателям, если они переданы */
        if (out_rssi_packet != NULL) {
            *out_rssi_packet = p_signal_int;
        }
        
        if (out_snr != NULL) {
            /* 
             * 2. Точный расчет SNR во float с шагом 0.5 dB
             * SNR = P(сигнала) - P(шума) -> (-raw_packet)/2 - (-raw_noise)/2
             */
            *out_snr = ((float)raw_noise - (float)raw_packet) / 2.0f;
        }

        return LORA_OK;
    }

    /* Если заголовок пакета поврежден или пришел мусор */
    return LORA_ERR_RECEIVE;
}
