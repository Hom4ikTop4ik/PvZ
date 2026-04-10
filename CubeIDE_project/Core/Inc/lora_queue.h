#ifndef LORA_QUEUE_H
#define LORA_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_config.h"

/* ============================================================================
 * TX Queue - кольцевой буфер сообщений LoRa
 * ============================================================================
 * Потокобезопасная очередь с защитой через __disable_irq() / __enable_irq().
 * Поддерживает pacing для контроля частоты отправки.
 * ============================================================================ */

/* Структура очереди TX (кольцевой буфер) */
typedef struct {
    uint8_t  buffer[LORA_TX_QUEUE_SIZE][LORA_MAX_PAYLOAD_LEN];  /* данные */
    uint16_t lengths[LORA_TX_QUEUE_SIZE];                       /* длины сообщений */
    uint8_t  head;      /* индекс чтения */
    uint8_t  tail;      /* индекс записи */
    uint8_t  count;     /* количество сообщений */
    uint32_t next_send_time;  /* время следующей отправки (для pacing) */
} lora_queue_t;

/* Инициализация */
void lora_queue_init(lora_queue_t *queue);

/* Состояние */
bool lora_queue_empty(const lora_queue_t *queue);
bool lora_queue_full(const lora_queue_t *queue);
uint8_t lora_queue_count(const lora_queue_t *queue);

/* Операции (потокобезопасные через отключение прерываний) */
bool lora_queue_add(lora_queue_t *queue, const uint8_t *data, uint16_t len);
bool lora_queue_pop(lora_queue_t *queue, uint8_t *out_data, uint16_t *out_len);
void lora_queue_clear(lora_queue_t *queue);

/* Pacing helpers */
bool lora_queue_can_send_now(const lora_queue_t *queue);
void lora_queue_mark_sent(lora_queue_t *queue);

#endif /* LORA_QUEUE_H */
