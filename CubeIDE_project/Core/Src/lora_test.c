// lora_test.c
#include "main.h"
#include "gpio.h"
#include "usart.h"
#include "lora_config.h"
#include "lora_test.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// ====================
// Минимальный тест LoRa передачи с STM32
// ====================

// Используйте готовый дескриптор LPUART1 (STM32CubeMX -> usart.c)
extern UART_HandleTypeDef huart1;

// Ждет, пока AUX=1 (модуль LoRa готов к работе)
bool wait_lora_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while(HAL_GetTick() - start < timeout_ms) {
        // Если AUX высокий - готов!
		GPIO_PinState aux = HAL_GPIO_ReadPin(LORA_AUX_PORT, LORA_AUX_PIN);
        if (aux == GPIO_PIN_SET) {
            return true;
        }
    }
    return false; // не дождались
}

// Функция отправки сообщения через LoRa
void send_lora_message(void)
{
    const char* msg = "Hello\r\n\0";
    uint16_t len = strlen(msg);

    // ДЛЯ МОДУЛЯ E22: дождаться готовности (AUX = 1)
    if (!wait_lora_ready(100)) {
        PRINTF("[LoRa TEST] AUX не готов, пропуск отправки смс\r\n");
        return;
    }

    // Отправляем строку через UART (LPUART1)
    HAL_StatusTypeDef res = HAL_UART_Transmit(&huart1, (uint8_t*)msg, len, 500);

    if (res == HAL_OK) {
        PRINTF("[LoRa TEST] Сообщение отправлено: %s\r\n", msg);
    } else {
        PRINTF("[LoRa TEST] Ошибка отправки! res=%d\r\n", res);
    }
}
