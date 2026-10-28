#ifndef CF09_MAX14830_BACKEND_FAKE_BUS_V1_H
#define CF09_MAX14830_BACKEND_FAKE_BUS_V1_H

#include "cf09_max14830_backend_v1.h"
#include "cf09_max14830_fake_spi_v2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CF09_MAX14830_BACKEND_FAKE_TRACE_CAPACITY = 64
};

typedef struct {
    uint8_t bytes[CF09_MAX14830_MAX_FRAME_BYTES];
    size_t len;
} cf09_max14830_backend_fake_trace_t;

typedef struct {
    bool initialized;
    cf09_max14830_fake_spi_t device;
    bool output_gate_enabled;
    bool reset_asserted;
    bool ready_ok;
    bool gate_ok;
    bool reset_ok;
    bool abort_ok;
    bool guard_arm_ok;
    bool guard_cancel_ok;
    bool quiesce_ok;
    bool quiesced;
    cf09_a_completion_path_health_t health;
    cf09_max14830_backend_io_result_t start_result;
    cf09_max14830_backend_io_result_t
        control_script[CF09_MAX14830_BACKEND_FAKE_TRACE_CAPACITY];
    size_t control_script_count;
    size_t control_script_cursor;
    cf09_max14830_backend_fake_trace_t
        control_trace[CF09_MAX14830_BACKEND_FAKE_TRACE_CAPACITY];
    size_t control_trace_count;
    cf09_max14830_spi_frame_t pending_async_frame;
    bool async_pending;
    bool guard_armed[CF09_MAX14830_BACKEND_TX_CHANNELS];
    cf09_a_attempt_token_t
        guard_token[CF09_MAX14830_BACKEND_TX_CHANNELS];
    uint64_t start_count;
    uint64_t abort_count;
    uint64_t gate_count;
    uint64_t reset_count;
    uint64_t ready_count;
    uint64_t guard_arm_count;
    uint64_t guard_cancel_count;
    uint64_t quiesce_count;
} cf09_max14830_backend_fake_bus_t;

void cf09_max14830_backend_fake_bus_init(
    cf09_max14830_backend_fake_bus_t *fake);

cf09_max14830_backend_platform_ops_t
cf09_max14830_backend_fake_bus_ops(
    cf09_max14830_backend_fake_bus_t *fake);

bool cf09_max14830_backend_fake_script_control(
    cf09_max14830_backend_fake_bus_t *fake,
    const cf09_max14830_backend_io_result_t *results,
    size_t result_count);

void cf09_max14830_backend_fake_set_revision(
    cf09_max14830_backend_fake_bus_t *fake,
    uint8_t revision);

bool cf09_max14830_backend_fake_complete_async(
    cf09_max14830_backend_fake_bus_t *fake,
    cf09_a_commit_result_t result);

bool cf09_max14830_backend_fake_raise_tail(
    cf09_max14830_backend_fake_bus_t *fake,
    uint8_t uart);

/* Simulates the scheduled guard firing and returns its copied token. */
bool cf09_max14830_backend_fake_fire_guard(
    cf09_max14830_backend_fake_bus_t *fake,
    uint8_t uart,
    cf09_a_attempt_token_t *token_out);

#endif
