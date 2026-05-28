#ifndef LORA_COMMAND_H
#define LORA_COMMAND_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_config.h"

/* ============================================================================
 * LoRa Command Parser & Handler
 * ============================================================================
 * Обрабатывает входящие команды от Hub и ретранслирует чужие пакеты.
 * Команды: SLEEP, FORCE_HUM/TMP/GEO/STT, HUM/TMP/GEO/STT _ON/_OFF
 * ============================================================================ */

/* Типы команд от Hub */
#define LORA_CMD_SLEEP      "SLEEP"
#define LORA_CMD_FORCE_HUM  "FORCE_HUM"
#define LORA_CMD_FORCE_TMP  "FORCE_TMP"
#define LORA_CMD_FORCE_GEO  "FORCE_GEO"
#define LORA_CMD_FORCE_STT  "FORCE_STT"
#define LORA_CMD_HUM_ON     "HUM_ON"
#define LORA_CMD_HUM_OFF    "HUM_OFF"
#define LORA_CMD_TMP_ON     "TMP_ON"
#define LORA_CMD_TMP_OFF    "TMP_OFF"
#define LORA_CMD_GEO_ON     "GEO_ON"
#define LORA_CMD_GEO_OFF    "GEO_OFF"
#define LORA_CMD_STT_ON     "STT_ON"
#define LORA_CMD_STT_OFF    "STT_OFF"

/* Тип команды как enum */
typedef enum {
    LORA_CMD_TYPE_UNKNOWN = 0,
    LORA_CMD_TYPE_SLEEP,
    LORA_CMD_TYPE_FORCE_HUM,
    LORA_CMD_TYPE_FORCE_TMP,
    LORA_CMD_TYPE_FORCE_GEO,
    LORA_CMD_TYPE_FORCE_STT,
    LORA_CMD_TYPE_HUM_ON,
    LORA_CMD_TYPE_HUM_OFF,
    LORA_CMD_TYPE_TMP_ON,
    LORA_CMD_TYPE_TMP_OFF,
    LORA_CMD_TYPE_GEO_ON,
    LORA_CMD_TYPE_GEO_OFF,
    LORA_CMD_TYPE_STT_ON,
    LORA_CMD_TYPE_STT_OFF
} lora_cmd_type_t;

/* Результат обработки */
typedef enum {
    LORA_CMD_OK = 0,
    LORA_CMD_UNKNOWN,
    LORA_CMD_ERROR
} lora_cmd_result_t;

/* Тип callback функции для ретрансляции (добавление в TX очередь) */
typedef bool (*lora_retransmit_callback_t)(const uint8_t *payload, uint16_t len);

/* Установка callback для ретрансляции */
void lora_command_set_retransmit_callback(lora_retransmit_callback_t callback);

/* Парсинг команды из строки */
lora_cmd_type_t lora_command_parse(const char *cmd_str);

/* Выполнение команды */
lora_cmd_result_t lora_command_execute(lora_cmd_type_t cmd);

/* Обработка полного payload (включая разбор device_id, проверку дубликатов, ретрансляцию) */
/* Возвращает: true = обработано, false = пропущено (дубликат или ошибка формата) */
bool lora_command_process_payload(const char *payload, const char *local_node_id);

#endif /* LORA_COMMAND_H */
