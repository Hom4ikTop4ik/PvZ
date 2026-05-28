#ifndef LORA_JOIN_H
#define LORA_JOIN_H

#include "lora_queue.h"

typedef enum {
    LORA_JOIN_NOT_CONTROL = 0,
    LORA_JOIN_CONSUMED,
    LORA_JOIN_RETRANSMIT
} lora_join_result_t;

void lora_join_init(void);
void lora_join_tick(void);
lora_join_result_t lora_join_process_payload(const char *payload);

#endif /* LORA_JOIN_H */
