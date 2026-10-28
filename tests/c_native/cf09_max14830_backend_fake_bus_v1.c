#include "cf09_max14830_backend_fake_bus_v1.h"

#include <limits.h>
#include <string.h>

static cf09_max14830_backend_io_result_t
cf09_max14830_backend_fake_control_transfer(
    void *user,
    const uint8_t *tx_bytes,
    size_t tx_len,
    uint8_t *rx_bytes,
    size_t rx_capacity,
    size_t *rx_len_out)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;
    cf09_max14830_backend_io_result_t scripted =
        CF09_MAX14830_BACKEND_IO_OK;
    cf09_max14830_spi_frame_t frame;
    cf09_max14830_result_t codec_result;

    if (rx_len_out == NULL) {
        return CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE;
    }
    *rx_len_out = 0U;
    if (fake == NULL || !fake->initialized || tx_bytes == NULL ||
        tx_len == 0U ||
        tx_len > (size_t)CF09_MAX14830_MAX_FRAME_BYTES ||
        fake->control_trace_count >=
            (size_t)CF09_MAX14830_BACKEND_FAKE_TRACE_CAPACITY) {
        return CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE;
    }
    memcpy(
        fake->control_trace[fake->control_trace_count].bytes,
        tx_bytes,
        tx_len);
    fake->control_trace[fake->control_trace_count].len = tx_len;
    fake->control_trace_count++;
    if (fake->control_script_cursor < fake->control_script_count) {
        scripted = fake->control_script[fake->control_script_cursor];
        fake->control_script_cursor++;
    }
    if (scripted != CF09_MAX14830_BACKEND_IO_OK) {
        return scripted;
    }
    memset(&frame, 0, sizeof(frame));
    memcpy(frame.bytes, tx_bytes, tx_len);
    frame.len = tx_len;
    codec_result = cf09_max14830_fake_transfer(
        &fake->device,
        &frame,
        rx_bytes,
        rx_capacity,
        rx_len_out);
    return codec_result == CF09_MAX14830_RESULT_OK ?
        CF09_MAX14830_BACKEND_IO_OK :
        CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE;
}

static cf09_max14830_backend_io_result_t
cf09_max14830_backend_fake_start_transfer(
    void *user,
    const uint8_t *tx_bytes,
    size_t tx_len)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized || tx_bytes == NULL ||
        tx_len == 0U ||
        tx_len > (size_t)CF09_MAX14830_MAX_FRAME_BYTES ||
        fake->start_count == UINT64_MAX || fake->async_pending) {
        return CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE;
    }
    fake->start_count++;
    if (fake->start_result == CF09_MAX14830_BACKEND_IO_OK ||
        fake->start_result ==
            CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE) {
        memset(&fake->pending_async_frame, 0,
               sizeof(fake->pending_async_frame));
        memcpy(fake->pending_async_frame.bytes, tx_bytes, tx_len);
        fake->pending_async_frame.len = tx_len;
        fake->async_pending = true;
    }
    return fake->start_result;
}

static bool cf09_max14830_backend_fake_abort_transfer(void *user)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized ||
        fake->abort_count == UINT64_MAX) {
        return false;
    }
    fake->abort_count++;
    if (!fake->abort_ok) {
        return false;
    }
    fake->async_pending = false;
    memset(&fake->pending_async_frame, 0,
           sizeof(fake->pending_async_frame));
    return true;
}

static bool cf09_max14830_backend_fake_set_reset(
    void *user,
    bool asserted)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;
    cf09_max14830_result_t result = CF09_MAX14830_RESULT_OK;

    if (fake == NULL || !fake->initialized || !fake->reset_ok ||
        fake->reset_count == UINT64_MAX) {
        return false;
    }
    fake->reset_count++;
    if (asserted && !fake->device.reset_active) {
        result = cf09_max14830_fake_assert_reset(&fake->device);
    } else if (!asserted && fake->device.reset_active) {
        result = cf09_max14830_fake_release_reset(&fake->device);
    }
    if (result != CF09_MAX14830_RESULT_OK) {
        return false;
    }
    fake->reset_asserted = asserted;
    return true;
}

static bool cf09_max14830_backend_fake_wait_ready(void *user)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized || !fake->ready_ok ||
        fake->ready_count == UINT64_MAX || fake->reset_asserted) {
        return false;
    }
    fake->ready_count++;
    return cf09_max14830_fake_mark_ready(&fake->device) ==
           CF09_MAX14830_RESULT_OK;
}

static bool cf09_max14830_backend_fake_set_output_gate(
    void *user,
    bool enabled)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized || !fake->gate_ok ||
        fake->gate_count == UINT64_MAX) {
        return false;
    }
    fake->gate_count++;
    fake->output_gate_enabled = enabled;
    return true;
}

static bool cf09_max14830_backend_fake_arm_guard(
    void *user,
    uint8_t uart,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized || token == NULL ||
        uart >= (uint8_t)CF09_MAX14830_BACKEND_TX_CHANNELS ||
        !fake->guard_arm_ok || fake->guard_armed[uart] ||
        fake->guard_arm_count == UINT64_MAX) {
        return false;
    }
    fake->guard_arm_count++;
    fake->guard_armed[uart] = true;
    fake->guard_token[uart] = *token;
    return true;
}

static bool cf09_max14830_backend_fake_cancel_guard(
    void *user,
    uint8_t uart,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized || token == NULL ||
        uart >= (uint8_t)CF09_MAX14830_BACKEND_TX_CHANNELS ||
        !fake->guard_cancel_ok || !fake->guard_armed[uart] ||
        !cf09_a_token_equal(&fake->guard_token[uart], token) ||
        fake->guard_cancel_count == UINT64_MAX) {
        return false;
    }
    fake->guard_cancel_count++;
    fake->guard_armed[uart] = false;
    memset(&fake->guard_token[uart], 0,
           sizeof(fake->guard_token[uart]));
    return true;
}

static bool cf09_max14830_backend_fake_quiesce(void *user)
{
    cf09_max14830_backend_fake_bus_t *fake =
        (cf09_max14830_backend_fake_bus_t *)user;
    size_t index;

    if (fake == NULL || !fake->initialized || !fake->quiesce_ok ||
        fake->quiesce_count == UINT64_MAX || fake->async_pending) {
        return false;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (fake->guard_armed[index]) {
            return false;
        }
    }
    fake->quiesce_count++;
    fake->quiesced = true;
    return true;
}

static cf09_a_completion_path_health_t
cf09_max14830_backend_fake_completion_health(void *user)
{
    const cf09_max14830_backend_fake_bus_t *fake =
        (const cf09_max14830_backend_fake_bus_t *)user;

    if (fake == NULL || !fake->initialized) {
        return CF09_A_COMPLETION_PATH_UNKNOWN;
    }
    return fake->health;
}

void cf09_max14830_backend_fake_bus_init(
    cf09_max14830_backend_fake_bus_t *fake)
{
    if (fake == NULL) {
        return;
    }
    memset(fake, 0, sizeof(*fake));
    fake->initialized = true;
    fake->ready_ok = true;
    fake->gate_ok = true;
    fake->reset_ok = true;
    fake->abort_ok = true;
    fake->guard_arm_ok = true;
    fake->guard_cancel_ok = true;
    fake->quiesce_ok = true;
    fake->health = CF09_A_COMPLETION_PATH_HEALTHY;
    fake->start_result = CF09_MAX14830_BACKEND_IO_OK;
    cf09_max14830_fake_spi_init(&fake->device);
    fake->reset_asserted = true;
}

cf09_max14830_backend_platform_ops_t
cf09_max14830_backend_fake_bus_ops(
    cf09_max14830_backend_fake_bus_t *fake)
{
    cf09_max14830_backend_platform_ops_t ops;

    memset(&ops, 0, sizeof(ops));
    if (fake == NULL || !fake->initialized) {
        return ops;
    }
    ops.user = fake;
    ops.control_transfer =
        cf09_max14830_backend_fake_control_transfer;
    ops.start_transfer = cf09_max14830_backend_fake_start_transfer;
    ops.abort_transfer = cf09_max14830_backend_fake_abort_transfer;
    ops.set_reset = cf09_max14830_backend_fake_set_reset;
    ops.wait_ready = cf09_max14830_backend_fake_wait_ready;
    ops.set_output_gate = cf09_max14830_backend_fake_set_output_gate;
    ops.arm_final_character_guard =
        cf09_max14830_backend_fake_arm_guard;
    ops.cancel_final_character_guard =
        cf09_max14830_backend_fake_cancel_guard;
    ops.quiesce = cf09_max14830_backend_fake_quiesce;
    ops.completion_health =
        cf09_max14830_backend_fake_completion_health;
    return ops;
}

bool cf09_max14830_backend_fake_script_control(
    cf09_max14830_backend_fake_bus_t *fake,
    const cf09_max14830_backend_io_result_t *results,
    size_t result_count)
{
    if (fake == NULL || !fake->initialized ||
        (results == NULL && result_count != 0U) ||
        result_count >
            (size_t)CF09_MAX14830_BACKEND_FAKE_TRACE_CAPACITY) {
        return false;
    }
    memset(fake->control_script, 0, sizeof(fake->control_script));
    if (result_count != 0U) {
        memcpy(fake->control_script, results,
               result_count * sizeof(results[0]));
    }
    fake->control_script_count = result_count;
    fake->control_script_cursor = 0U;
    return true;
}

void cf09_max14830_backend_fake_set_revision(
    cf09_max14830_backend_fake_bus_t *fake,
    uint8_t revision)
{
    if (fake != NULL && fake->initialized) {
        cf09_max14830_fake_set_revision(&fake->device, revision);
    }
}

bool cf09_max14830_backend_fake_complete_async(
    cf09_max14830_backend_fake_bus_t *fake,
    cf09_a_commit_result_t result)
{
    size_t rx_len = 0U;
    cf09_max14830_result_t codec_result;

    if (fake == NULL || !fake->initialized || !fake->async_pending ||
        (result != CF09_A_COMMIT_COMMITTED &&
         result != CF09_A_COMMIT_NOT_COMMITTED &&
         result != CF09_A_COMMIT_UNKNOWN)) {
        return false;
    }
    if (result == CF09_A_COMMIT_COMMITTED) {
        codec_result = cf09_max14830_fake_transfer(
            &fake->device,
            &fake->pending_async_frame,
            NULL,
            0U,
            &rx_len);
        if (codec_result != CF09_MAX14830_RESULT_OK) {
            return false;
        }
    }
    fake->async_pending = false;
    memset(&fake->pending_async_frame, 0,
           sizeof(fake->pending_async_frame));
    return true;
}

bool cf09_max14830_backend_fake_raise_tail(
    cf09_max14830_backend_fake_bus_t *fake,
    uint8_t uart)
{
    if (fake == NULL || !fake->initialized) {
        return false;
    }
    return cf09_max14830_fake_advance_to_final_character_start(
               &fake->device, uart) == CF09_MAX14830_RESULT_OK;
}

bool cf09_max14830_backend_fake_fire_guard(
    cf09_max14830_backend_fake_bus_t *fake,
    uint8_t uart,
    cf09_a_attempt_token_t *token_out)
{
    if (fake == NULL || !fake->initialized || token_out == NULL ||
        uart >= (uint8_t)CF09_MAX14830_BACKEND_TX_CHANNELS ||
        !fake->guard_armed[uart]) {
        return false;
    }
    *token_out = fake->guard_token[uart];
    fake->guard_armed[uart] = false;
    memset(&fake->guard_token[uart], 0,
           sizeof(fake->guard_token[uart]));
    return true;
}
