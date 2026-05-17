#include "lora_app.h"
#include "lora_driver.h"
#include "lora_packet.h"
#include "lora_command.h"
#include "lora_config.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Global Instances
 * ============================================================================ */
lora_queue_t lora_tx_queue;
lora_history_t lora_rx_history;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */
static void parse_msg_type_and_clear_force(const char *msg);

/* ============================================================================
 * Initialization
 * ============================================================================ */
bool lora_app_init(void)
{
    /* Инициализация компонентов */
    lora_queue_init(&lora_tx_queue);
    lora_history_init(&lora_rx_history);
    lora_packet_init();
    
    /* Установка callback для ретрансляции чужих пакетов */
    lora_command_set_retransmit_callback(lora_app_retransmit);
    
    /* Инициализация UART драйвера */
    if (lora_driver_init() != LORA_OK) {
        printf("[LoRa] Driver init failed\r\n");
        return false;
    }
    
    printf("[LoRa] Initialized OK\r\n");
    return true;
}

/* ============================================================================
 * TX Pump - прокачка очереди с pacing
 * ============================================================================
 * Отправляет одно сообщение из очереди если:
 * 1. Прошло достаточно времени с последней отправки (pacing)
 * 2. Очередь не пуста
 * 3. Модуль готов (AUX=1)
 * 
 * Важно: force-флаг сбрасывается ПОСЛЕ успешной отправки (здесь или выше)
 * ============================================================================ */
lora_pump_result_t lora_app_tx_pump(void)
{
    /* Проверка pacing */
    if (!lora_queue_can_send_now(&lora_tx_queue)) {
        return LORA_PUMP_WAIT;
    }
    
    /* Проверка очереди */
    if (lora_queue_empty(&lora_tx_queue)) {
        return LORA_PUMP_EMPTY;
    }
    
    /* Извлечение сообщения */
    uint8_t msg[LORA_MAX_PAYLOAD_LEN];
    uint16_t len;
    
    if (!lora_queue_pop(&lora_tx_queue, msg, &len)) {
        return LORA_PUMP_ERROR;
    }

    msg[len] = '\0';
    
    /* Парсим тип сообщения для сброса соответствующего force-флага */
    parse_msg_type_and_clear_force((const char *)msg);
    
    /* Проверка что модуль готов через AUX */
    if (!lora_driver_wait_ready(50)) {
        /* Модуль занят, возвращаем в очередь (не реализовано - упрощенно) */
        /* Для MVP: пропускаем отправку, ждем следующего цикла */
        return LORA_PUMP_WAIT;
    }
    
    /* Отправка через UART */
    lora_result_t result = lora_driver_send(msg, len);
    
    if (result == LORA_OK) {
        /* Отметка о отправке для pacing */
        lora_queue_mark_sent(&lora_tx_queue);
        printf("[LoRa] TX: %s\r\n", msg);
        return LORA_PUMP_SENT;
    }
    else {
        printf("[LoRa] TX error: %d\r\n", result);
        return LORA_PUMP_ERROR;
    }
}

/* ============================================================================
 * Parse message type and clear corresponding force flag
 * ============================================================================
 * Вызывается в момент pop из очереди (фактическая отправка)
 * Сбрасывает только тот force-флаг, который соответствует типу сообщения.
 * ============================================================================ */
static void parse_msg_type_and_clear_force(const char *msg)
{
    if (!msg) return;
    
    /* Ищем тип сообщения в строке: device_id;timestamp;id;TYPE;payload */
    /* Пропускаем 3 точки с запятой до TYPE */
    const char *p = msg;
    int semicolons = 0;
    
    while (*p && semicolons < 3) {
        if (*p == ';') semicolons++;
        p++;
    }
    
    if (semicolons < 3) return;  /* неверный формат */
    
    /* Сравниваем тип и сбрасываем соответствующий force-флаг */
    if (strncmp(p, LORA_MSG_HUM, 3) == 0) {
        lora_packet_clear_force_humidity();
    }
    else if (strncmp(p, LORA_MSG_TMP, 3) == 0) {
        lora_packet_clear_force_temperature();
    }
    else if (strncmp(p, LORA_MSG_GEO, 3) == 0) {
        lora_packet_clear_force_geo();
    }
    else if (strncmp(p, LORA_MSG_STT, 3) == 0) {
        lora_packet_clear_force_state();
    }
}

/* ============================================================================
 * RX Processing - обработка входящих пакетов
 * ============================================================================
 * Порядок обработки (dedup-first):
 * 1. Чтение строки из UART
 * 2. Парсинг ключа и проверка на дубликат (без полного разбора!)
 * 3. Если дубликат - пропускаем
 * 4. Если новый - добавляем в историю и обрабатываем (команда/ретрансляция)
 * ============================================================================ */
void lora_app_rx_process(void)
{
    uint8_t buffer[LORA_MAX_PAYLOAD_LEN];
    uint16_t len;
    
    /* Чтение строки из UART */
    if (!lora_driver_read_line(buffer, sizeof(buffer), &len)) {
        return;  /* нет данных или таймаут */
    }
    
    buffer[len] = '\0';

    printf("[LoRa] RX: received message: %s\r\n", (char*)buffer);

    /* Шаг 1: Парсинг ключа и проверка на дубликат (ТОЛЬКО ключ!) */
    lora_history_key_t key;
    if (!lora_history_parse_key((char *)buffer, &key)) {
        /* Не удалось распарсить ключ - неверный формат */
        printf("[LoRa] RX: invalid format - %s\r\n", (char*)buffer);
        return;
    }
    
    /* Шаг 2: Проверка на дубликат */
    if (lora_history_contains(&lora_rx_history, &key)) {
        /* Дубликат - игнорируем полностью */
        return;
    }
    
    /* Шаг 3: Новый пакет - добавляем в историю */
    lora_history_add(&lora_rx_history, &key);
    
    /* Шаг 4: Обработка команды или ретрансляция */
    lora_command_process_payload((char *)buffer, LORA_NODE_ID);
}

/* ============================================================================
 * Send Functions - добавление показаний в очередь
 * ============================================================================ */
bool lora_app_send_humidity(float humidity)
{
    if (!lora_packet_should_send_humidity()) {
        return false;
    }
    
    uint8_t buffer[LORA_MAX_PAYLOAD_LEN];
    memset(buffer, 0, sizeof(buffer));
    uint16_t len = lora_packet_build_humidity(buffer, sizeof(buffer), humidity);
    
    if (len == 0) return false;
    
    return lora_queue_add(&lora_tx_queue, buffer, len);
}

bool lora_app_send_temperature(float temp)
{
    if (!lora_packet_should_send_temperature()) {
        return false;
    }
    
    uint8_t buffer[LORA_MAX_PAYLOAD_LEN];
    memset(buffer, 0, sizeof(buffer));
    uint16_t len = lora_packet_build_temperature(buffer, sizeof(buffer), temp);
    
    if (len == 0) return false;
    
    return lora_queue_add(&lora_tx_queue, buffer, len);
}

bool lora_app_send_geo(float lat, float lon)
{
    if (!lora_packet_should_send_geo()) {
        return false;
    }
    
    uint8_t buffer[LORA_MAX_PAYLOAD_LEN];
    memset(buffer, 0, sizeof(buffer));
    uint16_t len = lora_packet_build_geo(buffer, sizeof(buffer), lat, lon);
    
    if (len == 0) return false;
    
    return lora_queue_add(&lora_tx_queue, buffer, len);
}

bool lora_app_send_battery(float percentage, float voltage)
{
    if (!lora_packet_should_send_state()) {
        return false;
    }
    
    uint8_t buffer[LORA_MAX_PAYLOAD_LEN];
    memset(buffer, 0, sizeof(buffer));
    /* Используем build_state с фиктивными rssi/snr для совместимости формата */
    uint16_t len = lora_packet_build_state(buffer, sizeof(buffer), 
                                            -100, 5.0f, percentage, true);
    
    if (len == 0) return false;
    
    return lora_queue_add(&lora_tx_queue, buffer, len);
}

bool lora_app_send_state(int16_t rssi, float snr, float battery, bool online)
{
    if (!lora_packet_should_send_state()) {
        return false;
    }
    
    uint8_t buffer[LORA_MAX_PAYLOAD_LEN];
    memset(buffer, 0, sizeof(buffer));
    uint16_t len = lora_packet_build_state(buffer, sizeof(buffer), 
                                            rssi, snr, battery, online);
    
    if (len == 0) return false;
    
    return lora_queue_add(&lora_tx_queue, buffer, len);
}

/* ============================================================================
 * Retransmit Callback - ретрансляция чужих пакетов
 * ============================================================================
 * Вызывается из lora_command при получении чужого пакета.
 * Добавляет пакет в TX очередь для ретрансляции.
 * ============================================================================ */
bool lora_app_retransmit(const uint8_t *payload, uint16_t len)
{
    if (!payload || len == 0 || len > LORA_MAX_PAYLOAD_LEN) {
        return false;
    }
    
    return lora_queue_add(&lora_tx_queue, payload, len);
}
