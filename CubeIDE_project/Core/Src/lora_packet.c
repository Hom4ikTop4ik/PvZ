#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "rtc.h"

#include "lora_packet.h"
#include "lora_identity.h"
#include "utils.h"

/* ============================================================================
 * Global Flags - управление отправкой
 * ============================================================================ */
volatile bool lora_enable_humidity = true;
volatile bool lora_enable_temperature = true;
volatile bool lora_enable_gps = true;
volatile bool lora_enable_status = true;

volatile bool force_humidity = false;
volatile bool force_temperature = false;
volatile bool force_gps = false;
volatile bool force_status = false;

/* Инициализация флагов */
void lora_packet_init(void)
{
    lora_enable_humidity = true;
    lora_enable_temperature = true;
    lora_enable_gps = true;
    lora_enable_status = true;
    
    force_humidity = false;
    force_temperature = false;
    force_gps = false;
    force_status = false;
}

/* Простой random (LCG) - для msg_rnd_id достаточно */
uint32_t lora_packet_random_id(void)
{
    static uint32_t seed = 12345;
    seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
    return seed % 1000000;  /* 0-999999 */
}

/* Timestamp: используем HAL_GetTick как приблизительное время в секундах */
void lora_packet_timestamp(char *buffer, uint8_t len)
{
    //uint32_t seconds = HAL_GetTick() / 1000;
    //snprintf(buffer, len, "%lu", (unsigned long)seconds);

    rtc_iso8601_string(buffer, len, &hrtc);
}

/* ============================================================================
 * Packet Builders - формирование строк пакетов
 * Формат: device_id;timestamp;msg_rnd_id;type;payload
 * ============================================================================ */

/* Влажность (пример): NODE_ID;123456789;123456;hum;45.50 */
// Возвращает длину пакета или 0 при ошибке
uint16_t lora_packet_build_humidity(uint8_t *buffer, uint16_t max_len, float humidity)
{
    if (!buffer || max_len == 0) return 0;
    
    char timestamp[32];
    lora_packet_timestamp(timestamp, sizeof(timestamp));
    uint32_t rnd = lora_packet_random_id();
    
    int written = snprintf((char *)buffer, max_len, "%s;%s;%lu;%s;%.2f",
                          lora_identity_get_node_id(),
						  timestamp,
						  (unsigned long)rnd,
						  LORA_MSG_HUM,
						  humidity);
    
    return (written > 0 && (uint16_t)written < max_len) ? (uint16_t)written : 0;
}

/* Температура (пример): NODE_ID;123456789;123456;tmp;23.50 */
// Возвращает длину пакета или 0 при ошибке
uint16_t lora_packet_build_temperature(uint8_t *buffer, uint16_t max_len, float temperature)
{
    if (!buffer || max_len == 0) return 0;
    
    char timestamp[32];
    lora_packet_timestamp(timestamp, sizeof(timestamp));
    uint32_t rnd = lora_packet_random_id();
    
    int written = snprintf((char *)buffer, max_len, "%s;%s;%lu;%s;%.2f",
                          lora_identity_get_node_id(),
						  timestamp,
						  (unsigned long)rnd,
						  LORA_MSG_TMP,
						  temperature);
    
    return (written > 0 && (uint16_t)written < max_len) ? (uint16_t)written : 0;
}

/* Координаты (пример): NODE_ID;123456789;123456;gps;55.755826,37.617300 */
// Возвращает длину пакета или 0 при ошибке
uint16_t lora_packet_build_gps(uint8_t *buffer, uint16_t max_len, float lat, float lon)
{
    if (!buffer || max_len == 0) return 0;
    
    char timestamp[32];
    lora_packet_timestamp(timestamp, sizeof(timestamp));
    uint32_t rnd = lora_packet_random_id();
    
    int written = snprintf((char *)buffer, max_len, "%s;%s;%lu;%s;%.6f,%.6f",
                          lora_identity_get_node_id(),
						  timestamp,
						  (unsigned long)rnd,
						  LORA_MSG_GEO,
						  lat, lon);
    
    return (written > 0 && (uint16_t)written < max_len) ? (uint16_t)written : 0;
}

/* Статус (пример): NODE_ID;123456789;123456;stt;-120,5.50,95.0,online */
// Возвращает длину пакета или 0 при ошибке
uint16_t lora_packet_build_state(uint8_t *buffer, uint16_t max_len, 
                                   int16_t rssi, float snr, float battery, bool online)
{
    if (!buffer || max_len == 0) return 0;
    
    char timestamp[32];
    lora_packet_timestamp(timestamp, sizeof(timestamp));
    uint32_t rnd = lora_packet_random_id();
    
    int written = snprintf((char *)buffer, max_len, "%s;%s;%lu;%s;%d,%.2f,%.1f,%s",
                          lora_identity_get_node_id(),
						  timestamp,
						  (unsigned long)rnd,
						  LORA_MSG_STT,
                          (int)rssi, snr, battery, online ? "online" : "offline");
    
    return (written > 0 && (uint16_t)written < max_len) ? (uint16_t)written : 0;
}

/* Пакет подключения */
// Возвращает длину пакета или 0 при ошибке
uint16_t lora_packet_build_join(uint8_t *buffer, uint16_t max_len, const char *node_identity_mac)
{
    if (!buffer || max_len == 0) return 0;

    char timestamp[32];
    lora_packet_timestamp(timestamp, sizeof(timestamp));
    uint32_t rnd = lora_packet_random_id();

    int written = snprintf((char*)buffer, max_len, "%s;%s;%lu;%s;%s",
                           lora_identity_get_node_id(),
                           timestamp,
                           (unsigned long)rnd,
                           LORA_MSG_JOIN,
                           node_identity_mac);

    return (written > 0 && (uint16_t)written < max_len) ? (uint16_t)written : 0;
}

/* ============================================================================
 * Should Send Checks - проверка флагов
 * ============================================================================ */
bool lora_packet_should_send_humidity(void)
{
    return lora_enable_humidity || force_humidity;
}

bool lora_packet_should_send_temperature(void)
{
    return lora_enable_temperature || force_temperature;
}

bool lora_packet_should_send_gps(void)
{
    return lora_enable_gps || force_gps;
}

bool lora_packet_should_send_state(void)
{
    return lora_enable_status || force_status;
}

/* ============================================================================
 * Per-Sensor Force Flag Clearing - сброс только конкретного флага
 * ============================================================================
 * Это позволяет сбрасывать force-флаг только после фактической отправки
 * конкретного типа сообщения, а не все сразу.
 * ============================================================================ */

void lora_packet_clear_force_humidity(void)
{
    force_humidity = false;
}

void lora_packet_clear_force_temperature(void)
{
    force_temperature = false;
}

void lora_packet_clear_force_gps(void)
{
    force_gps = false;
}

void lora_packet_clear_force_state(void)
{
    force_status = false;
}

/* Сброс всех force-флагов (для совместимости) */
void lora_packet_clear_force_all(void)
{
    force_humidity = false;
    force_temperature = false;
    force_gps = false;
    force_status = false;
}
