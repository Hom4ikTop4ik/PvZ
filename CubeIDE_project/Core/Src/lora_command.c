#include "lora_command.h"
#include "lora_packet.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Command Parser & Handler Implementation
 * ============================================================================ */

/* Callback для ретрансляции чужих пакетов */
static lora_retransmit_callback_t retransmit_cb = NULL;

/* Установка callback для ретрансляции */
void lora_command_set_retransmit_callback(lora_retransmit_callback_t callback)
{
    retransmit_cb = callback;
}

/* Парсинг команды из строки в enum */
lora_cmd_type_t lora_command_parse(const char *cmd_str)
{
    if (!cmd_str) return LORA_CMD_TYPE_UNKNOWN;
    
    if (strcmp(cmd_str, LORA_CMD_SLEEP) == 0) return LORA_CMD_TYPE_SLEEP;
    if (strcmp(cmd_str, LORA_CMD_FORCE_HUM) == 0) return LORA_CMD_TYPE_FORCE_HUM;
    if (strcmp(cmd_str, LORA_CMD_FORCE_TMP) == 0) return LORA_CMD_TYPE_FORCE_TMP;
    if (strcmp(cmd_str, LORA_CMD_FORCE_GEO) == 0) return LORA_CMD_TYPE_FORCE_GEO;
    if (strcmp(cmd_str, LORA_CMD_FORCE_STT) == 0) return LORA_CMD_TYPE_FORCE_STT;
    if (strcmp(cmd_str, LORA_CMD_HUM_ON) == 0) return LORA_CMD_TYPE_HUM_ON;
    if (strcmp(cmd_str, LORA_CMD_HUM_OFF) == 0) return LORA_CMD_TYPE_HUM_OFF;
    if (strcmp(cmd_str, LORA_CMD_TMP_ON) == 0) return LORA_CMD_TYPE_TMP_ON;
    if (strcmp(cmd_str, LORA_CMD_TMP_OFF) == 0) return LORA_CMD_TYPE_TMP_OFF;
    if (strcmp(cmd_str, LORA_CMD_GEO_ON) == 0) return LORA_CMD_TYPE_GEO_ON;
    if (strcmp(cmd_str, LORA_CMD_GEO_OFF) == 0) return LORA_CMD_TYPE_GEO_OFF;
    if (strcmp(cmd_str, LORA_CMD_STT_ON) == 0) return LORA_CMD_TYPE_STT_ON;
    if (strcmp(cmd_str, LORA_CMD_STT_OFF) == 0) return LORA_CMD_TYPE_STT_OFF;
    
    return LORA_CMD_TYPE_UNKNOWN;
}

/* Выполнение команды - устанавливает соответствующие флаги */
lora_cmd_result_t lora_command_execute(lora_cmd_type_t cmd)
{
    switch (cmd) {
        case LORA_CMD_TYPE_SLEEP:
            /* TODO: реализовать sleep режим STM32 (STOP mode) */
            printf("[CMD] SLEEP requested - sleeping 3s\r\n");
            HAL_Delay(3000);  /* Временная заглушка для тестов */
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_FORCE_HUM:
            lora_force_humidity = true;
            printf("[CMD] FORCE_HUM set\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_FORCE_TMP:
            lora_force_temperature = true;
            printf("[CMD] FORCE_TMP set\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_FORCE_GEO:
            lora_force_geo = true;
            printf("[CMD] FORCE_GEO set\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_FORCE_STT:
            lora_force_status = true;
            printf("[CMD] FORCE_STT set\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_HUM_ON:
            lora_enable_humidity = true;
            printf("[CMD] HUM_ON\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_HUM_OFF:
            lora_enable_humidity = false;
            printf("[CMD] HUM_OFF\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_TMP_ON:
            lora_enable_temperature = true;
            printf("[CMD] TMP_ON\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_TMP_OFF:
            lora_enable_temperature = false;
            printf("[CMD] TMP_OFF\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_GEO_ON:
            lora_enable_geo = true;
            printf("[CMD] GEO_ON\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_GEO_OFF:
            lora_enable_geo = false;
            printf("[CMD] GEO_OFF\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_STT_ON:
            lora_enable_status = true;
            printf("[CMD] STT_ON\r\n");
            return LORA_CMD_OK;
            
        case LORA_CMD_TYPE_STT_OFF:
            lora_enable_status = false;
            printf("[CMD] STT_OFF\r\n");
            return LORA_CMD_OK;
            
        default:
            return LORA_CMD_UNKNOWN;
    }
}

/* ============================================================================
 * Payload Processing - дедупликация, команды, ретрансляция
 * ============================================================================
 * Формат payload: device_id;timestamp;msg_rnd_id;type;data
 * 
 * Логика:
 * 1. Парсим device_id, timestamp, msg_rnd_id, type, data
 * 2. Если device_id == local_node_id: проверяем команды
 * 3. Иначе: ретранслируем через callback
 * ============================================================================ */
bool lora_command_process_payload(const char *payload, const char *local_node_id)
{
    if (!payload || !local_node_id) return false;
    
    /* Копируем для модификации (strtok разрушает строку) */
    char buf[LORA_MAX_PAYLOAD_LEN];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    /* Разбор по точке с запятой: device_id;timestamp;msg_rnd_id;type;data */
    char *device_id = strtok(buf, ";");
    char *timestamp = strtok(NULL, ";");
    char *msg_rnd_id = strtok(NULL, ";");
    char *msg_type = strtok(NULL, ";");
    char *msg_data = strtok(NULL, ";");
    
    if (!device_id || !timestamp || !msg_rnd_id || !msg_type) {
        return false;  /* неверный формат - недостаточно полей */
    }
    
    /* Команды приходят как "cmd" тип, data содержит саму команду */
    if (strcmp(device_id, local_node_id) == 0) {
        /* Пакет для нас - обрабатываем команды */
        if (strcmp(msg_type, LORA_MSG_CMD) == 0 && msg_data) {
            /* Парсим и выполняем команду */
            lora_cmd_type_t cmd = lora_command_parse(msg_data);
            if (cmd != LORA_CMD_TYPE_UNKNOWN) {
                lora_command_execute(cmd);
            }
        }
        return true;
    }
    else {
        /* Чужой пакет - ретранслируем */
        printf("[ECHO] Retransmit from %s\r\n", device_id);
        
        if (retransmit_cb) {
            /* Добавляем в TX очередь для ретрансляции */
            retransmit_cb((const uint8_t *)payload, strlen(payload));
        }
        
        return true;
    }
}
