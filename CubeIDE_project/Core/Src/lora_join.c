#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lora_app.h"
#include "lora_join.h"
#include "lora_config.h"
#include "lora_identity.h"
#include "lora_packet.h"

#include "printf.h"

static uint32_t last_join_ms;

static char *next_field(char **cursor)
{
    char *start;
    char *sep;

    if (!cursor || !*cursor) {
        return NULL;
    }

    start = *cursor;
    sep = strchr(start, ';');
    if (sep) {
        *sep = '\0';
        *cursor = sep + 1;
    } else {
        *cursor = NULL;
    }

    return start;
}

static char *next_csv(char **cursor)
{
    char *start;
    char *sep;

    if (!cursor || !*cursor) {
        return NULL;
    }

    start = *cursor;
    sep = strchr(start, ',');
    if (sep) {
        *sep = '\0';
        *cursor = sep + 1;
    } else {
        *cursor = NULL;
    }

    return start;
}

// set last_join_ms and last_join_rnd to zero
void lora_join_init(void)
{
    last_join_ms = 0U;
}

void lora_join_tick()
{
    if (lora_identity_is_assigned())
        return; // JOIN не нужен

    uint32_t now = HAL_GetTick();
    if (last_join_ms != 0U && (now - last_join_ms) < LORA_JOIN_INTERVAL_MS)
        return; // Rate limit

    const char *node_mac = lora_identity_get_mac();
    if (lora_app_send_join(node_mac)) {
        last_join_ms = now;
        PRINTF("[JOIN] Request added to lora-tx-queue\r\n");
    }
}

lora_join_result_t lora_join_process_payload(const char *payload)
{
    char work[LORA_MAX_PAYLOAD_LEN];
    char data[LORA_MAX_PAYLOAD_LEN];
    char *cursor;
    char *device_id;
    char *timestamp;
    char *msg_rnd;
    char *msg_type;
    char *msg_data;
    char *csv_cursor;
    char *mac;
    char *node_id;

    PRINTF("[JOIN] 1) lora_join_process_payload, payload: %s\r\n", payload);

    if (!payload) {
        return LORA_JOIN_NOT_CONTROL;
    }

    strncpy(work, payload, sizeof(work) - 1U);
    work[sizeof(work) - 1U] = '\0';

    cursor = work;
    device_id = next_field(&cursor);
    timestamp = next_field(&cursor);
    msg_rnd = next_field(&cursor);
    msg_type = next_field(&cursor);
    msg_data = cursor;

    (void)timestamp;
    (void)msg_rnd;

    PRINTF("[JOIN] 2.1) before checkings\r\n");
    if (!device_id || !msg_type || !msg_data) {
    	PRINTF("[JOIN] 2.1) error 1\r\n");
        return LORA_JOIN_NOT_CONTROL;
    }

    if (strcmp(msg_type, LORA_MSG_JOIN) == 0) {
    	PRINTF("[JOIN] 2.1) error 2\r\n");
        return LORA_JOIN_RETRANSMIT;
    }

    if (strcmp(msg_type, LORA_MSG_JOIN_ACK) != 0) {
    	PRINTF("[JOIN] 2.1) error 3\r\n");
        return LORA_JOIN_NOT_CONTROL;
    }

    PRINTF("[JOIN] 2.2) after checkings\r\n");

    strncpy(data, msg_data, sizeof(data) - 1U);
    data[sizeof(data) - 1U] = '\0';

    csv_cursor = data;
    mac = next_csv(&csv_cursor);
    node_id = next_csv(&csv_cursor);

    PRINTF("[JOIN] 3.1) before checkings\r\n");

    if (!mac || !node_id) {
        PRINTF("[JOIN] Invalid join_ack payload\r\n");
        return LORA_JOIN_CONSUMED;
    }

    if (strcmp(mac, lora_identity_get_mac()) != 0) {
        return LORA_JOIN_RETRANSMIT;
    }

    PRINTF("[JOIN] 3.2) after checkings - set_node '%s'\r\n", node_id);

    lora_identity_set_node_id(node_id);
    return LORA_JOIN_CONSUMED;
}
