#ifndef LORA_HISTORY_H
#define LORA_HISTORY_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_config.h"

/* ============================================================================
 * Deduplication History - защита от повторной обработки одинаковых пакетов
 * ============================================================================
 * Хранит 128 последних ключей (device_hash, timestamp, msg_rnd_id).
 * Кольцевой буфер со смещением head - старые ключи вытесняются автоматически.
 * ============================================================================ */

/* Ключ для дедупликации: device_id + timestamp + msg_rnd_id */
typedef struct {
    uint32_t device_hash;   /* хеш device_id (FNV-1a) */
    uint32_t timestamp;     /* timestamp */
    uint32_t msg_rnd_id;    /* случайный ID сообщения */
} lora_history_key_t;

/* Структура истории (кольцевой буфер ключей) */
typedef struct {
    lora_history_key_t keys[LORA_HISTORY_SIZE];
    uint16_t head;
    uint16_t count;
} lora_history_t;

/* Инициализация */
void lora_history_init(lora_history_t *hist);

/* Проверка наличия ключа (true = найден/дубликат, false = новый) */
bool lora_history_contains(const lora_history_t *hist, const lora_history_key_t *key);

/* Добавление ключа */
void lora_history_add(lora_history_t *hist, const lora_history_key_t *key);

/* Парсинг ключа из строки пакета "device_id;timestamp;msg_rnd_id;type;payload" */
bool lora_history_parse_key(const char *payload, lora_history_key_t *out_key);

/* Очистка истории */
void lora_history_clear(lora_history_t *hist);

#endif /* LORA_HISTORY_H */
