#ifndef LORA_IDENTITY_H
#define LORA_IDENTITY_H

#include <stdbool.h>

void lora_identity_init(void);
const char *lora_identity_get_node_id(void);
const char *lora_identity_get_mac(void);
bool lora_identity_is_assigned(void);
bool lora_identity_set_node_id(const char *node_id);

#endif /* LORA_IDENTITY_H */
