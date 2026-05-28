#include "lora_history.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Deduplication History Implementation
 * ============================================================================ */

/* Простой хеш строки (FNV-1a) - быстрый и достаточно равномерный */
static uint32_t hash_string(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)(*str++);
        hash *= 16777619u;
    }
    return hash;
}

/* Инициализация истории */
void lora_history_init(lora_history_t *hist)
{
    if (!hist) return;
    memset(hist, 0, sizeof(lora_history_t));
}

/* Проверка наличия ключа в истории (true = найден дубликат) */
bool lora_history_contains(const lora_history_t *hist, const lora_history_key_t *key)
{
    if (!hist || !key) return false;
    
    /* Линейный поиск по истории (для 128 элементов достаточно быстро) */
    /* Поиск от newest (head-1) к oldest для раннего выхода на дубликатах */
    for (uint16_t i = 0; i < hist->count; i++) {
        uint16_t idx = (hist->head + LORA_HISTORY_SIZE - 1 - i) % LORA_HISTORY_SIZE;
        const lora_history_key_t *stored = &hist->keys[idx];
        
        if (stored->device_hash == key->device_hash &&
            stored->timestamp == key->timestamp &&
            stored->msg_rnd_id == key->msg_rnd_id) {
            return true;  /* найден дубликат */
        }
    }
    
    return false;
}

/* Добавление ключа в историю */
void lora_history_add(lora_history_t *hist, const lora_history_key_t *key)
{
    if (!hist || !key) return;
    
    /* Запись в текущую позицию head */
    hist->keys[hist->head] = *key;
    hist->head = (hist->head + 1) % LORA_HISTORY_SIZE;
    
    if (hist->count < LORA_HISTORY_SIZE) {
        hist->count++;
    }
}

/* Парсинг ключа из строки пакета формата "device_id;timestamp;msg_rnd_id;type;payload" */
bool lora_history_parse_key(const char *payload, lora_history_key_t *out_key)
{
    if (!payload || !out_key) return false;
    
    /* Копируем для модификации (strtok разрушает строку) */
    char buf[LORA_MAX_PAYLOAD_LEN];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    /* Разбор по точке с запятой */
    /* device_id;timestamp;msg_rnd_id;type;payload */
    char *device_id = strtok(buf, ";");
    char *timestamp_str = strtok(NULL, ";");
    char *msg_rnd_id_str = strtok(NULL, ";");
    /* msg_type и msg_data игнорируем при парсинге ключа */
    
    if (!device_id || !timestamp_str || !msg_rnd_id_str) {
        return false;  /* неверный формат - недостаточно полей */
    }
    
    out_key->device_hash = hash_string(device_id);
    out_key->timestamp = (uint32_t)strtoul(timestamp_str, NULL, 10);
    out_key->msg_rnd_id = (uint32_t)strtoul(msg_rnd_id_str, NULL, 10);
    
    return true;
}

/* Очистка истории */
void lora_history_clear(lora_history_t *hist)
{
    if (!hist) return;
    memset(hist, 0, sizeof(lora_history_t));
}
