#include "lora_identity.h"
#include "lora_config.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <string.h>

static char current_node_id[LORA_NODE_ID_MAX_LEN] = { 0 };
static char node_mac[LORA_NODE_MAC_MAX_LEN] = { 0 };
static bool node_id_assigned;

// init MAC and default node_ID
void lora_identity_init(void)
{
    snprintf(current_node_id, sizeof(current_node_id), "%s", LORA_UNASSIGNED_NODE_ID);
    node_id_assigned = false;

#ifdef LORA_NODE_MAC_OVERRIDE
    snprintf(node_mac, sizeof(node_mac), "%s", LORA_NODE_MAC_OVERRIDE);
#else
    snprintf(node_mac, sizeof(node_mac), "%08lX%08lX%08lX",
             (unsigned long)HAL_GetUIDw0(),
             (unsigned long)HAL_GetUIDw1(),
             (unsigned long)HAL_GetUIDw2());
#endif
}

const char *lora_identity_get_node_id(void)
{
    return current_node_id;
}

const char *lora_identity_get_mac(void)
{
    return node_mac;
}

bool lora_identity_is_assigned(void)
{
    return node_id_assigned;
}

bool lora_identity_set_node_id(const char *node_id)
{
    if (!node_id || node_id[0] == '\0') {
        return false;
    }

    if (strcmp(node_id, LORA_UNASSIGNED_NODE_ID) == 0) {
        return false;
    }

    snprintf(current_node_id, sizeof(current_node_id), "%s", node_id);
    node_id_assigned = true;
    printf("[JOIN] DONE -  Node_ID assigned: %s (MAC %s)\r\n", current_node_id, node_mac);
    return true;
}
