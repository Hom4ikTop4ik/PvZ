#ifndef LORA_PACKET_H
#define LORA_PACKET_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_config.h"

#define LORA_MSG_JOIN   "join"
#define LORA_MSG_JOIN_ACK "join_ack"

/* ============================================================================
* LoRa Packet Builder
* ============================================================================
* Формирует текстовые пакеты формата: device_id;timestamp;msg_rnd_id;type;payload
* Поддерживает типы: hum, tmp, gps, stt (humidity, temperature, gps, status)
* ============================================================================ */

/* Типы сообщений (строковые константы) */


#define LORA_MSG_HUM    "hum"   /* влажность */
#define LORA_MSG_TMP    "tmp"   /* температура */
#define LORA_MSG_GEO    "gps"   /* координаты */
#define LORA_MSG_STT    "stt"   /* статус */
#define LORA_MSG_CMD    "cmd"   /* команда */
#define LORA_MSG_JOIN   "join"  /* подключение к mesh-сети*/
#define LORA_MSG_JOIN_ACK "join_ack"

/* Флаги включения отправки замеров (глобальные переменные) */
extern volatile bool lora_enable_humidity;
extern volatile bool lora_enable_temperature;
extern volatile bool lora_enable_gps;
extern volatile bool lora_enable_status;

/* Флаги принудительного измерения */
extern volatile bool force_humidity;
extern volatile bool force_temperature;
extern volatile bool force_gps;
extern volatile bool force_status;

/* Инициализация флагов */
void lora_packet_init(void);

/* Генерация случайного ID сообщения (0-999999) */
uint32_t lora_packet_random_id(void);

/* Получение текущего timestamp (время в секундах с момента старта) */
void lora_packet_timestamp(char *buffer, uint8_t len);

/* Формирование пакетов (возвращают длину строки или 0 при ошибке) */
uint16_t lora_packet_build_humidity(uint8_t *buffer, uint16_t max_len, float humidity);
uint16_t lora_packet_build_temperature(uint8_t *buffer, uint16_t max_len, float temperature);
uint16_t lora_packet_build_gps(uint8_t *buffer, uint16_t max_len, float lat, float lon);
uint16_t lora_packet_build_state(uint8_t *buffer, uint16_t max_len, 
                                   int16_t rssi, float snr, float battery, bool online);
uint16_t lora_packet_build_join(uint8_t *buffer, uint16_t max_len, const char *node_identity_mac);

/* Проверка нужно ли отправлять (по флагам enable или force) */
bool lora_packet_should_send_humidity(void);
bool lora_packet_should_send_temperature(void);
bool lora_packet_should_send_gps(void);
bool lora_packet_should_send_state(void);

/* Сброс флагов принудительного измерения (по отдельности для каждого типа) */
void lora_packet_clear_force_humidity(void);
void lora_packet_clear_force_temperature(void);
void lora_packet_clear_force_gps(void);
void lora_packet_clear_force_state(void);

/* Сброс всех force-флагов (для совместимости) */
void lora_packet_clear_force_all(void);

#endif /* LORA_PACKET_H */
