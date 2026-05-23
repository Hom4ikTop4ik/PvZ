#include "lora_join.h"
#include "lora_config.h"
#include "lora_identity.h"
#include "lora_packet.h"

#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t last_join_ms;
static uint32_t last_join_rnd;

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

void lora_join_init(void)
{
    last_join_ms = 0U;
    last_join_rnd = 0U;
}

void lora_join_process_tick(lora_queue_t *tx_queue)
{
    uint32_t now;
    char timestamp[32];
    uint8_t payload[LORA_MAX_PAYLOAD_LEN];
    int written;

    if (!tx_queue || lora_identity_is_assigned()) {
        return;
    }

    now = HAL_GetTick();
    if (last_join_ms != 0U && (now - last_join_ms) < LORA_JOIN_INTERVAL_MS) {
        return;
    }

    lora_packet_timestamp(timestamp, sizeof(timestamp));
    last_join_rnd = lora_packet_random_id();

    written = snprintf((char *)payload, sizeof(payload), "%s;%s;%lu;%s;%s",
                       LORA_UNASSIGNED_NODE_ID,
                       timestamp,
                       (unsigned long)last_join_rnd,
                       LORA_MSG_JOIN,
                       lora_identity_get_mac());

    if (written <= 0 || written >= (int)sizeof(payload)) {
        return;
    }

    if (lora_queue_add(tx_queue, payload, (uint16_t)written)) {
        last_join_ms = now;
        printf("[JOIN] Request queued: mac=%s rnd=%lu\r\n",
               lora_identity_get_mac(),
               (unsigned long)last_join_rnd);
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
    char *request_rnd;
    char *node_id;
    uint32_t request_value;

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

    if (!device_id || !msg_type || !msg_data) {
        return LORA_JOIN_NOT_CONTROL;
    }

    if (strcmp(msg_type, LORA_MSG_JOIN) == 0) {
        return LORA_JOIN_RETRANSMIT;
    }

    if (strcmp(msg_type, LORA_MSG_JOIN_ACK) != 0) {
        return LORA_JOIN_NOT_CONTROL;
    }

    strncpy(data, msg_data, sizeof(data) - 1U);
    data[sizeof(data) - 1U] = '\0';

    csv_cursor = data;
    mac = next_csv(&csv_cursor);
    request_rnd = next_csv(&csv_cursor);
    node_id = next_csv(&csv_cursor);

    if (!mac || !request_rnd || !node_id) {
        printf("[JOIN] Invalid join_ack payload\r\n");
        return LORA_JOIN_CONSUMED;
    }

    if (strcmp(mac, lora_identity_get_mac()) != 0) {
        return LORA_JOIN_RETRANSMIT;
    }

    request_value = (uint32_t)strtoul(request_rnd, NULL, 10);
    if (request_value != last_join_rnd) {
        printf("[JOIN] join_ack rnd mismatch: got=%lu expected=%lu\r\n",
               (unsigned long)request_value,
               (unsigned long)last_join_rnd);
        return LORA_JOIN_CONSUMED;
    }

    lora_identity_set_node_id(node_id);
    return LORA_JOIN_CONSUMED;
}
