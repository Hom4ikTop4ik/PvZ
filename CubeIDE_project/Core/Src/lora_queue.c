#include "lora_queue.h"
#include <string.h>

/* ============================================================================
 * TX Queue Implementation
 * ============================================================================
 * Все модифицирующие операции защищены отключением прерываний,
 * так как очередь используется из main loop (pop) и из обработчиков RX (add).
 * ============================================================================ */

/* Инициализация очереди */
void lora_queue_init(lora_queue_t *queue)
{
    if (!queue) return;
    
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    
    memset(queue, 0, sizeof(lora_queue_t));
    
    __set_PRIMASK(primask);
}

/* Проверка на пустоту (read-only, не требует защиты) */
bool lora_queue_empty(const lora_queue_t *queue)
{
    return (!queue || queue->count == 0);
}

/* Проверка на заполненность (read-only, не требует защиты) */
bool lora_queue_full(const lora_queue_t *queue)
{
    return (!queue || queue->count >= LORA_TX_QUEUE_SIZE);
}

/* Количество сообщений (read-only, не требует защиты) */
uint8_t lora_queue_count(const lora_queue_t *queue)
{
    return queue ? queue->count : 0;
}

/* Добавление сообщения в очередь (потокобезопасно) */
bool lora_queue_add(lora_queue_t *queue, const uint8_t *data, uint16_t len)
{
    if (!queue || !data || len == 0 || len > LORA_MAX_PAYLOAD_LEN) {
        return false;
    }
    
    /* Критическая секция */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    
    if (queue->count >= LORA_TX_QUEUE_SIZE) {
        __set_PRIMASK(primask);
        return false;  /* очередь переполнена */
    }
    
    /* Копирование данных */
    memcpy(queue->buffer[queue->tail], data, len);
    queue->lengths[queue->tail] = len;
    
    /* Сдвиг tail */
    queue->tail = (queue->tail + 1) % LORA_TX_QUEUE_SIZE;
    queue->count++;
    
    __set_PRIMASK(primask);
    return true;
}

/* Извлечение сообщения из очереди (потокобезопасно) */
bool lora_queue_pop(lora_queue_t *queue, uint8_t *out_data, uint16_t *out_len)
{
    if (!queue || !out_data || !out_len) {
        return false;
    }
    
    /* Критическая секция */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    
    if (queue->count == 0) {
        __set_PRIMASK(primask);
        return false;  /* очередь пуста */
    }
    
    /* Копирование данных */
    *out_len = queue->lengths[queue->head];
    memcpy(out_data, queue->buffer[queue->head], *out_len);
    
    /* Очистка (необязательно, но для безопасности) */
    memset(queue->buffer[queue->head], 0, LORA_MAX_PAYLOAD_LEN);
    queue->lengths[queue->head] = 0;
    
    /* Сдвиг head */
    queue->head = (queue->head + 1) % LORA_TX_QUEUE_SIZE;
    queue->count--;
    
    __set_PRIMASK(primask);
    return true;
}

/* Очистка очереди (потокобезопасно) */
void lora_queue_clear(lora_queue_t *queue)
{
    if (!queue) return;
    
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    
    memset(queue->buffer, 0, sizeof(queue->buffer));
    memset(queue->lengths, 0, sizeof(queue->lengths));
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    
    __set_PRIMASK(primask);
}

/* Проверка можно ли отправлять (pacing) - read-only */
bool lora_queue_can_send_now(const lora_queue_t *queue)
{
    if (!queue) return false;
    
    uint32_t now = HAL_GetTick();
    return (now >= queue->next_send_time);
}

/* Отметка о отправке (установка таймера pacing) - модифицирует поле, но атомарно */
void lora_queue_mark_sent(lora_queue_t *queue)
{
    if (!queue) return;
    
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    
    queue->next_send_time = HAL_GetTick() + LORA_PACING_INTERVAL_MS;
    
    __set_PRIMASK(primask);
}
