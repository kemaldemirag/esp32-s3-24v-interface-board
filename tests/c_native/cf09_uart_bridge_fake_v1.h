#ifndef CF09_UART_BRIDGE_FAKE_V1_H
#define CF09_UART_BRIDGE_FAKE_V1_H

#include "cf09_uart_bridge_v1.h"

typedef struct {
    bool active;
    bool outputs_enabled;
    bool close_succeeds;
    bool abort_succeeds;
    bool output_change_succeeds;
    bool quiesce_succeeds;
    bool quiesced;
    cf09_a_completion_path_health_t completion_path_health;
    cf09_a_backend_start_result_t next_start_result;
    cf09_a_attempt_token_t active_token;
    uint8_t *active_payload;
    size_t observed_payload_len;
    uint64_t observed_payload_fingerprint;
    bool channel_seen[CF09_A_EXTERNAL_TX_COUNT];
    cf09_a_attempt_token_t last_started_by_channel[CF09_A_EXTERNAL_TX_COUNT];
    uint64_t last_payload_fingerprint_by_channel[CF09_A_EXTERNAL_TX_COUNT];
    uint64_t start_count;
    uint64_t close_count;
    uint64_t abort_count;
    uint64_t enable_count;
    uint64_t disable_count;
    uint64_t quiesce_count;
} cf09_a_fake_backend_t;

void cf09_a_fake_backend_init(cf09_a_fake_backend_t *fake);

cf09_a_backend_ops_t cf09_a_fake_backend_ops(
    cf09_a_fake_backend_t *fake);

void cf09_a_fake_set_next_start_result(
    cf09_a_fake_backend_t *fake,
    cf09_a_backend_start_result_t result);

void cf09_a_fake_set_close_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds);

void cf09_a_fake_set_abort_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds);

void cf09_a_fake_set_output_change_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds);

void cf09_a_fake_set_quiesce_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds);

void cf09_a_fake_set_completion_path_health(
    cf09_a_fake_backend_t *fake,
    cf09_a_completion_path_health_t health);

#endif
