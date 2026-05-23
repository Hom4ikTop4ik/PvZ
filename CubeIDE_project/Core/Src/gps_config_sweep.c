#include <stdbool.h>
#include <stdio.h>

#include "gps_config_sweep.h"
#include "printf.h"

#define UBX_ACK_TIMEOUT_MS 1500U
#define UBX_PAYLOAD_MAX 96U
#define GPS_AFTER_RESET_WAIT_MS 5000U

#define FLAG_OFF             0x00000000UL
#define FLAG_ENABLE          0x00000001UL
#define FLAG_ENABLE_L1       0x00010001UL
#define FLAG_ENABLE_L1_ALT   0x01000001UL

typedef struct {
    uint8_t gnss_id;
    uint8_t res_trk_ch;
    uint8_t max_trk_ch;
    uint32_t flags;
} gnss_block_t;

typedef struct {
    const char *name;
    uint8_t num_trk_ch_hw;
    uint8_t num_trk_ch_use;
    uint8_t block_count;
    const gnss_block_t *blocks;
} gnss_config_variant_t;

static const gnss_block_t blocks_legacy_glonass[] = {
    {0U, 4U, 0xFFU, FLAG_OFF},
    {1U, 0U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE},
};

static const gnss_block_t blocks_legacy_gps_glonass[] = {
    {0U, 4U, 0xFFU, FLAG_ENABLE},
    {1U, 0U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE},
};

static const gnss_block_t blocks_neo7_glonass[] = {
    {0U, 4U, 0xFFU, FLAG_OFF},
    {1U, 1U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE},
};

static const gnss_block_t blocks_neo7_gps_glonass[] = {
    {0U, 4U, 0xFFU, FLAG_ENABLE},
    {1U, 1U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE},
};

static const gnss_block_t blocks_neo7_gps_only[] = {
    {0U, 4U, 0xFFU, FLAG_ENABLE},
    {1U, 1U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 0U, 0xFFU, FLAG_OFF},
};

static const gnss_block_t blocks_neo7_glonass_l1_mask[] = {
    {0U, 4U, 0xFFU, FLAG_OFF},
    {1U, 1U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE_L1},
};

static const gnss_block_t blocks_neo7_gps_glonass_l1_mask[] = {
    {0U, 4U, 0xFFU, FLAG_ENABLE_L1},
    {1U, 1U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE_L1},
};

static const gnss_block_t blocks_legacy_glonass_l1_alt_mask[] = {
    {0U, 4U, 0xFFU, FLAG_OFF},
    {1U, 0U, 0x03U, FLAG_OFF},
    {5U, 0U, 0x03U, FLAG_OFF},
    {6U, 8U, 0xFFU, FLAG_ENABLE_L1_ALT},
};

static const gnss_config_variant_t variants[] = {
    {
        "legacy-32ch-glonass-enable-bit",
        0x20U,
        0x20U,
        4U,
        blocks_legacy_glonass,
    },
    {
        "legacy-32ch-gps-glonass-enable-bit",
        0x20U,
        0x20U,
        4U,
        blocks_legacy_gps_glonass,
    },
    {
        "neo7-22ch-glonass-enable-bit",
        0x16U,
        0x16U,
        4U,
        blocks_neo7_glonass,
    },
    {
        "neo7-22ch-gps-glonass-enable-bit",
        0x16U,
        0x16U,
        4U,
        blocks_neo7_gps_glonass,
    },
    {
        "neo7-22ch-gps-only-enable-bit",
        0x16U,
        0x16U,
        4U,
        blocks_neo7_gps_only,
    },
    {
        "neo7-22ch-glonass-l1-mask-0x00010001",
        0x16U,
        0x16U,
        4U,
        blocks_neo7_glonass_l1_mask,
    },
    {
        "neo7-22ch-gps-glonass-l1-mask-0x00010001",
        0x16U,
        0x16U,
        4U,
        blocks_neo7_gps_glonass_l1_mask,
    },
    {
        "legacy-32ch-glonass-alt-mask-0x01000001",
        0x20U,
        0x20U,
        4U,
        blocks_legacy_glonass_l1_alt_mask,
    },
};

static const uint8_t cfg_save_payload[] = {
    0x00, 0x00, 0x00, 0x00,
    0x1F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x1F,
};

static const uint8_t cfg_cold_start_payload[] = {
    0xFF, 0xFF, 0x01, 0x00,
};

static void ubx_checksum_update(uint8_t *ck_a, uint8_t *ck_b, uint8_t value)
{
    *ck_a = (uint8_t)(*ck_a + value);
    *ck_b = (uint8_t)(*ck_b + *ck_a);
}

static void gps_config_flush_rx(UART_HandleTypeDef *uart)
{
    uint8_t byte;

    for (uint16_t i = 0U; i < 512U; ++i) {
        if (HAL_UART_Receive(uart, &byte, 1U, 1U) != HAL_OK) {
            break;
        }
    }
}

static uint16_t build_cfg_gnss_payload(const gnss_config_variant_t *variant,
                                       uint8_t *payload,
                                       uint16_t payload_size)
{
    uint16_t pos = 4U;

    if (variant == NULL || payload == NULL || payload_size < 4U ||
        payload_size < (uint16_t)(4U + (8U * variant->block_count))) {
        return 0U;
    }

    payload[0] = 0x00U;
    payload[1] = variant->num_trk_ch_hw;
    payload[2] = variant->num_trk_ch_use;
    payload[3] = variant->block_count;

    for (uint8_t i = 0U; i < variant->block_count; ++i) {
        uint32_t flags = variant->blocks[i].flags;

        payload[pos++] = variant->blocks[i].gnss_id;
        payload[pos++] = variant->blocks[i].res_trk_ch;
        payload[pos++] = variant->blocks[i].max_trk_ch;
        payload[pos++] = 0x00U;
        payload[pos++] = (uint8_t)(flags & 0xFFU);
        payload[pos++] = (uint8_t)((flags >> 8) & 0xFFU);
        payload[pos++] = (uint8_t)((flags >> 16) & 0xFFU);
        payload[pos++] = (uint8_t)((flags >> 24) & 0xFFU);
    }

    return pos;
}

static void ubx_send(UART_HandleTypeDef *uart, uint8_t msg_class, uint8_t msg_id,
                     const uint8_t *payload, uint16_t payload_len)
{
    uint8_t header[6] = {
        0xB5, 0x62, msg_class, msg_id,
        (uint8_t)(payload_len & 0xFFU),
        (uint8_t)(payload_len >> 8),
    };
    uint8_t checksum[2] = {0U, 0U};

    for (uint16_t i = 2U; i < sizeof(header); ++i) {
        ubx_checksum_update(&checksum[0], &checksum[1], header[i]);
    }
    for (uint16_t i = 0U; i < payload_len; ++i) {
        ubx_checksum_update(&checksum[0], &checksum[1], payload[i]);
    }

    HAL_UART_Transmit(uart, header, sizeof(header), HAL_MAX_DELAY);
    if (payload_len > 0U && payload != NULL) {
        HAL_UART_Transmit(uart, (uint8_t *)payload, payload_len, HAL_MAX_DELAY);
    }
    HAL_UART_Transmit(uart, checksum, sizeof(checksum), HAL_MAX_DELAY);
}

static bool ubx_read_packet(UART_HandleTypeDef *uart, uint8_t *out_class, uint8_t *out_id,
                            uint8_t *payload, uint16_t payload_size, uint16_t *out_len,
                            uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t state = 0U;
    uint8_t cls = 0U;
    uint8_t id = 0U;
    uint16_t len = 0U;
    uint16_t payload_index = 0U;
    uint8_t ck_a = 0U;
    uint8_t ck_b = 0U;
    uint8_t rx_ck_a = 0U;
    uint8_t byte = 0U;

    if (out_len != NULL) {
        *out_len = 0U;
    }

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (HAL_UART_Receive(uart, &byte, 1U, 20U) != HAL_OK) {
            continue;
        }

        switch (state) {
        case 0:
            state = (byte == 0xB5U) ? 1U : 0U;
            break;
        case 1:
            state = (byte == 0x62U) ? 2U : 0U;
            break;
        case 2:
            cls = byte;
            ck_a = 0U;
            ck_b = 0U;
            ubx_checksum_update(&ck_a, &ck_b, byte);
            state = 3U;
            break;
        case 3:
            id = byte;
            ubx_checksum_update(&ck_a, &ck_b, byte);
            state = 4U;
            break;
        case 4:
            len = byte;
            ubx_checksum_update(&ck_a, &ck_b, byte);
            state = 5U;
            break;
        case 5:
            len |= ((uint16_t)byte << 8);
            ubx_checksum_update(&ck_a, &ck_b, byte);
            payload_index = 0U;
            if (len > payload_size) {
                state = 0U;
            } else if (len == 0U) {
                state = 7U;
            } else {
                state = 6U;
            }
            break;
        case 6:
            payload[payload_index++] = byte;
            ubx_checksum_update(&ck_a, &ck_b, byte);
            if (payload_index >= len) {
                state = 7U;
            }
            break;
        case 7:
            rx_ck_a = byte;
            state = 8U;
            break;
        case 8:
            if (rx_ck_a == ck_a && byte == ck_b) {
                if (out_class != NULL) {
                    *out_class = cls;
                }
                if (out_id != NULL) {
                    *out_id = id;
                }
                if (out_len != NULL) {
                    *out_len = len;
                }
                return true;
            }
            state = 0U;
            break;
        default:
            state = 0U;
            break;
        }
    }

    return false;
}

static int ubx_wait_ack(UART_HandleTypeDef *uart, uint8_t ack_class, uint8_t ack_id)
{
    uint32_t start = HAL_GetTick();
    uint8_t cls = 0U;
    uint8_t id = 0U;
    uint8_t payload[UBX_PAYLOAD_MAX] = {0U};
    uint16_t len = 0U;

    while ((HAL_GetTick() - start) < UBX_ACK_TIMEOUT_MS) {
        if (!ubx_read_packet(uart, &cls, &id, payload, sizeof(payload), &len, 200U)) {
            continue;
        }

        if (cls == 0x05U && len == 2U &&
            payload[0] == ack_class && payload[1] == ack_id) {
            if (id == 0x01U) {
                return 1;
            }
            if (id == 0x00U) {
                return -1;
            }
        }
    }

    return 0;
}

static void save_and_reset_if_enabled(UART_HandleTypeDef *uart)
{
#if GPS_CONFIG_SAVE_ACCEPTED
    PRINTF("Saving accepted GNSS config\r\n");
    ubx_send(uart, 0x06U, 0x09U, cfg_save_payload, sizeof(cfg_save_payload));
    HAL_Delay(1000U);
#endif

#if GPS_CONFIG_COLD_START_ACCEPTED
    PRINTF("Cold-starting GPS module after accepted config\r\n");
    ubx_send(uart, 0x06U, 0x04U, cfg_cold_start_payload, sizeof(cfg_cold_start_payload));
    PRINTF("Waiting after cold start (%lu ms)\r\n", (unsigned long)GPS_AFTER_RESET_WAIT_MS);
    HAL_Delay(GPS_AFTER_RESET_WAIT_MS);
#endif
}

static bool try_variant(UART_HandleTypeDef *uart, const gnss_config_variant_t *variant)
{
    uint8_t payload[UBX_PAYLOAD_MAX] = {0U};
    uint16_t payload_len;
    int ack;

    payload_len = build_cfg_gnss_payload(variant, payload, sizeof(payload));
    if (payload_len == 0U) {
        PRINTF("GPS config variant skipped: %s payload build failed\r\n", variant->name);
        return false;
    }

    PRINTF("Trying GPS config: %s\r\n", variant->name);
    gps_config_flush_rx(uart);
    ubx_send(uart, 0x06U, 0x3EU, payload, payload_len);
    ack = ubx_wait_ack(uart, 0x06U, 0x3EU);

    if (ack > 0) {
        PRINTF("GPS config accepted: %s\r\n", variant->name);
        save_and_reset_if_enabled(uart);
        return true;
    }

    if (ack < 0) {
        PRINTF("GPS config rejected: %s\r\n", variant->name);
    } else {
        PRINTF("GPS config timeout: %s\r\n", variant->name);
    }

    HAL_Delay(300U);
    return false;
}

static void run_legacy_python_fallback(UART_HandleTypeDef *uart)
{
    uint8_t payload[UBX_PAYLOAD_MAX] = {0U};
    uint16_t payload_len;

    PRINTF("No ACK-checked config worked; running legacy Python fallback\r\n");
    payload_len = build_cfg_gnss_payload(&variants[0], payload, sizeof(payload));
    if (payload_len == 0U) {
        PRINTF("Legacy fallback payload build failed\r\n");
        return;
    }

    gps_config_flush_rx(uart);
    PRINTF("Applying legacy GNSS config: GPS/SBAS/QZSS off, GLONASS on\r\n");
    ubx_send(uart, 0x06U, 0x3EU, payload, payload_len);
    HAL_Delay(1000U);

    PRINTF("Saving GNSS config\r\n");
    ubx_send(uart, 0x06U, 0x09U, cfg_save_payload, sizeof(cfg_save_payload));
    HAL_Delay(1000U);

    PRINTF("Cold-starting GPS module\r\n");
    ubx_send(uart, 0x06U, 0x04U, cfg_cold_start_payload, sizeof(cfg_cold_start_payload));
    PRINTF("Waiting after cold start (%lu ms)\r\n", (unsigned long)GPS_AFTER_RESET_WAIT_MS);
    HAL_Delay(GPS_AFTER_RESET_WAIT_MS);
}

void gps_config_sweep_run(UART_HandleTypeDef *gps_uart)
{
    if (gps_uart == NULL) {
        return;
    }

    PRINTF("GPS config auto-sweep: trying CFG-GNSS variants until first ACK\r\n");

    for (uint32_t i = 0U; i < (sizeof(variants) / sizeof(variants[0])); ++i) {
        if (try_variant(gps_uart, &variants[i])) {
            PRINTF("GPS config auto-sweep stopped after accepted variant\r\n");
            return;
        }
    }

#if GPS_CONFIG_LEGACY_FALLBACK_IF_ALL_FAIL
    run_legacy_python_fallback(gps_uart);
#else
    PRINTF("GPS config auto-sweep finished: no accepted variant\r\n");
#endif
}
