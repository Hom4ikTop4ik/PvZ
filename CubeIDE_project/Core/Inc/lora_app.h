#ifndef LORA_APP_H
#define LORA_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_queue.h"
#include "lora_history.h"

/* ============================================================================
 * LoRa Application Layer
 * ============================================================================
 * Интеграционный слой: связывает все компоненты LoRa.
 * Глобальные экземпляры очереди и истории, основной цикл pump + RX processing.
 * ============================================================================ */

/* Глобальные экземпляры (доступны для всех модулей) */
extern lora_queue_t lora_tx_queue;
extern lora_history_t lora_rx_history;

/* Инициализация всей подсистемы LoRa */
bool lora_app_init(void);

/* Результат прокачки TX очереди */
typedef enum {
    LORA_PUMP_EMPTY = 0,    /* очередь пуста */
    LORA_PUMP_WAIT,         /* pacing - еще рано */
    LORA_PUMP_SENT,         /* отправлено */
    LORA_PUMP_ERROR         /* ошибка */
} lora_pump_result_t;

/* Прокачка TX очереди (вызывать из main loop часто) */
lora_pump_result_t lora_app_tx_pump(void);

/* Обработка RX (вызывать при наличии данных) - порядок: dedup → parse → handle */
void lora_app_rx_process(void);

/* Отправка показаний датчиков в очередь */
bool lora_app_send_humidity(float humidity);
bool lora_app_send_temperature(float temp);
bool lora_app_send_battery(float percentage, float voltage);
bool lora_app_send_geo(float lat, float lon);
bool lora_app_send_state(int16_t rssi, float snr, float battery, bool online);

/* Ретрансляция чужого пакета (callback для lora_command) */
bool lora_app_retransmit(const uint8_t *payload, uint16_t len);

#endif /* LORA_APP_H */
