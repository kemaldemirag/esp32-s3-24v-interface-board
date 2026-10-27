#include "cf09_uart_bridge_v1.h"

#include <limits.h>
#include <string.h>

static const cf09_a_route_t CF09_A_ROUTES[CF09_A_EXTERNAL_TX_COUNT] = {
    {2U, 2U, 0U, 0U, "TTL2", "RS2322"},
    {3U, 3U, 1U, 1U, "TTL3", "RS2323"},
    {4U, 4U, 2U, 2U, "TTL4", "RS2324"}
};

static bool cf09_a_session_equal(
    cf09_a_session_id_t left,
    cf09_a_session_id_t right)
{
    return left.high == right.high && left.low == right.low;
}

static bool cf09_a_session_valid(cf09_a_session_id_t session)
{
    return session.high != 0U || session.low != 0U;
}

static bool cf09_a_payload_valid(const uint8_t *payload, size_t payload_len)
{
    return payload_len == 0U || payload != NULL;
}

static bool cf09_a_physical_channel_valid(uint8_t physical_channel)
{
    return physical_channel < (uint8_t)CF09_A_EXTERNAL_TX_COUNT;
}

static bool cf09_a_logical_to_index(
    uint8_t logical_channel,
    size_t *out_index)
{
    if (out_index == NULL) {
        return false;
    }
    if (logical_channel < (uint8_t)CF09_A_EXTERNAL_LOGICAL_FIRST ||
        logical_channel > (uint8_t)CF09_A_EXTERNAL_LOGICAL_LAST) {
        *out_index = 0U;
        return false;
    }
    *out_index = (size_t)(logical_channel -
                          (uint8_t)CF09_A_EXTERNAL_LOGICAL_FIRST);
    return true;
}

static cf09_a_result_t cf09_a_enter(cf09_a_driver_t *driver)
{
    if (driver == NULL || !driver->_initialized) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_mutating) {
        return CF09_A_RESULT_REENTRANT;
    }
    driver->_mutating = true;
    return CF09_A_RESULT_OK;
}

static void cf09_a_leave(cf09_a_driver_t *driver)
{
    driver->_mutating = false;
}

static void cf09_a_increment_rejected(cf09_a_driver_t *driver)
{
    if (driver->_rejected_callback_count == UINT64_MAX) {
        driver->_counter_exhausted = true;
        return;
    }
    driver->_rejected_callback_count++;
}

static void cf09_a_increment_uncertain(cf09_a_driver_t *driver)
{
    if (driver->_uncertain_count == UINT64_MAX) {
        driver->_counter_exhausted = true;
        return;
    }
    driver->_uncertain_count++;
}

static void cf09_a_mark_slot_uncertain(
    cf09_a_driver_t *driver,
    cf09_a_attempt_slot_t *slot)
{
    if (slot->occupied && slot->state != CF09_A_ATTEMPT_COMPLETE &&
        slot->state != CF09_A_ATTEMPT_UNCERTAIN) {
        slot->state = CF09_A_ATTEMPT_UNCERTAIN;
        cf09_a_increment_uncertain(driver);
    }
}

static bool cf09_a_token_route_valid(
    const cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    size_t *out_index)
{
    const cf09_a_route_t *route;
    size_t index;

    if (driver == NULL || token == NULL || out_index == NULL) {
        return false;
    }
    if (!cf09_a_logical_to_index(token->logical_channel, &index)) {
        return false;
    }
    route = &CF09_A_ROUTES[index];
    if (!cf09_a_session_equal(token->session, driver->_session) ||
        token->serializer_id != route->serializer_id ||
        token->resource_id != route->resource_id ||
        token->physical_channel != route->physical_channel ||
        token->device_id != (uint8_t)CF09_A_DEVICE_ID ||
        token->chip_select_id != (uint8_t)CF09_A_CHIP_SELECT_ID) {
        return false;
    }
    *out_index = index;
    return true;
}

static bool cf09_a_token_generations_current(
    const cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    size_t index)
{
    return token->resource_generation == driver->_resource_generation[index] &&
           token->bus_generation == driver->_bus_generation &&
           token->device_generation == driver->_device_generation;
}

static cf09_a_attempt_slot_t *cf09_a_current_slot(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    bool require_current_generations,
    size_t *out_index)
{
    cf09_a_attempt_slot_t *slot;
    size_t index;

    if (!cf09_a_token_route_valid(driver, token, &index)) {
        return NULL;
    }
    slot = &driver->_slots[index];
    if (!slot->occupied || !cf09_a_token_equal(&slot->token, token)) {
        return NULL;
    }
    if (require_current_generations &&
        !cf09_a_token_generations_current(driver, token, index)) {
        return NULL;
    }
    if (out_index != NULL) {
        *out_index = index;
    }
    return slot;
}

static void cf09_a_fail_closed(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *possibly_active_token,
    bool backend_fault);

static cf09_a_result_t cf09_a_reject_callback(cf09_a_driver_t *driver)
{
    cf09_a_increment_rejected(driver);
    if (driver->_counter_exhausted) {
        cf09_a_fail_closed(driver, NULL, false);
        return CF09_A_RESULT_COUNTER_EXHAUSTED;
    }
    return CF09_A_RESULT_STALE_TOKEN;
}

static bool cf09_a_all_resources_ready(const cf09_a_driver_t *driver)
{
    size_t index;

    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        if (!driver->_configured[index] || !driver->_idle[index] ||
            driver->_configured_generation[index] !=
                driver->_resource_generation[index] ||
            driver->_idle_generation[index] !=
                driver->_resource_generation[index]) {
            return false;
        }
    }
    return true;
}

static bool cf09_a_has_blocking_attempt(const cf09_a_driver_t *driver)
{
    size_t index;

    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        const cf09_a_attempt_slot_t *slot = &driver->_slots[index];
        if (slot->occupied && slot->state != CF09_A_ATTEMPT_COMPLETE) {
            return true;
        }
    }
    return false;
}

static bool cf09_a_tx_ready(const cf09_a_driver_t *driver)
{
    return driver->_outputs_armed && driver->_bridge_up &&
           !driver->_reset_active && !driver->_backend_fault &&
           !driver->_counter_exhausted && cf09_a_all_resources_ready(driver);
}

static bool cf09_a_frame_is_newer(
    const cf09_a_driver_t *driver,
    size_t index,
    cf09_a_frame_id_t frame)
{
    const cf09_a_frame_id_t previous = driver->_last_frame[index];

    if (!driver->_frame_seen[index]) {
        return true;
    }
    return frame.epoch > previous.epoch ||
           (frame.epoch == previous.epoch &&
            frame.sequence > previous.sequence);
}

static void cf09_a_clear_host_pending(cf09_a_driver_t *driver)
{
    driver->_host_pending = false;
    driver->_abort_pending = false;
    memset(&driver->_host_pending_token, 0, sizeof(driver->_host_pending_token));
}

static void cf09_a_fail_closed(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *possibly_active_token,
    bool backend_fault)
{
    bool disabled;
    bool aborted;
    size_t index;

    disabled = driver->_backend.set_outputs_enabled(
        driver->_backend.user,
        false);
    driver->_outputs_armed = false;
    if (!disabled || backend_fault) {
        driver->_backend_fault = true;
    }

    if (driver->_host_pending) {
        aborted = driver->_backend.abort_host_tx(
            driver->_backend.user,
            &driver->_host_pending_token);
        if (aborted) {
            cf09_a_clear_host_pending(driver);
        } else {
            driver->_abort_pending = true;
            driver->_backend_fault = true;
        }
    } else if (possibly_active_token != NULL) {
        aborted = driver->_backend.abort_host_tx(
            driver->_backend.user,
            possibly_active_token);
        if (!aborted) {
            driver->_host_pending = true;
            driver->_abort_pending = true;
            driver->_host_pending_token = *possibly_active_token;
            driver->_backend_fault = true;
        }
    }

    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        cf09_a_mark_slot_uncertain(driver, &driver->_slots[index]);
    }
}

static cf09_a_result_t cf09_a_start_candidate(
    cf09_a_driver_t *driver,
    cf09_a_attempt_slot_t *slot,
    const cf09_a_attempt_token_t *candidate,
    const uint8_t *payload,
    size_t payload_len,
    cf09_a_attempt_token_t *out_token)
{
    cf09_a_backend_start_result_t backend_result;

    backend_result = driver->_backend.start_tx(
        driver->_backend.user,
        candidate,
        payload,
        payload_len);
    if (backend_result == CF09_A_BACKEND_BUSY) {
        return CF09_A_RESULT_BUSY;
    }
    if (backend_result == CF09_A_BACKEND_REJECTED) {
        return CF09_A_RESULT_BACKEND_REJECTED;
    }
    if (backend_result == CF09_A_BACKEND_FAILURE) {
        cf09_a_fail_closed(driver, NULL, true);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    if (backend_result == CF09_A_BACKEND_UNKNOWN_ACCEPTANCE) {
        slot->occupied = true;
        slot->state = CF09_A_ATTEMPT_UNCERTAIN;
        slot->token = *candidate;
        driver->_last_transfer_no = candidate->transfer_no;
        *out_token = *candidate;
        cf09_a_increment_uncertain(driver);
        cf09_a_fail_closed(driver, candidate, true);
        return driver->_counter_exhausted ?
            CF09_A_RESULT_COUNTER_EXHAUSTED : CF09_A_RESULT_UNCERTAIN;
    }
    if (backend_result != CF09_A_BACKEND_STARTED) {
        slot->occupied = true;
        slot->state = CF09_A_ATTEMPT_UNCERTAIN;
        slot->token = *candidate;
        driver->_last_transfer_no = candidate->transfer_no;
        *out_token = *candidate;
        cf09_a_increment_uncertain(driver);
        cf09_a_fail_closed(driver, candidate, true);
        return driver->_counter_exhausted ?
            CF09_A_RESULT_COUNTER_EXHAUSTED : CF09_A_RESULT_UNCERTAIN;
    }

    slot->occupied = true;
    slot->state = CF09_A_ATTEMPT_HOST_PENDING;
    slot->token = *candidate;
    driver->_host_pending = true;
    driver->_host_pending_token = *candidate;
    driver->_last_transfer_no = candidate->transfer_no;
    *out_token = *candidate;
    return CF09_A_RESULT_OK;
}

static cf09_a_result_t cf09_a_reset_common(
    cf09_a_driver_t *driver,
    bool bus_reset)
{
    cf09_a_result_t entered;
    bool backend_ok = true;
    bool quarantined = false;
    bool overflow = false;
    size_t index;

    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }

    if (!driver->_backend.set_outputs_enabled(
            driver->_backend.user,
            false)) {
        backend_ok = false;
        driver->_backend_fault = true;
    }
    driver->_outputs_armed = false;

    if (driver->_host_pending) {
        if (!driver->_backend.abort_host_tx(
                driver->_backend.user,
                &driver->_host_pending_token)) {
            backend_ok = false;
            driver->_backend_fault = true;
            driver->_abort_pending = true;
        } else {
            cf09_a_clear_host_pending(driver);
        }
    }

    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        cf09_a_attempt_slot_t *slot = &driver->_slots[index];
        if (slot->occupied && slot->state != CF09_A_ATTEMPT_COMPLETE &&
            slot->state != CF09_A_ATTEMPT_UNCERTAIN) {
            quarantined = true;
            cf09_a_mark_slot_uncertain(driver, slot);
        }
        driver->_configured[index] = false;
        driver->_idle[index] = false;
    }

    driver->_reset_active = true;
    driver->_bridge_up = false;
    driver->_completed_route_mask = 0U;

    if (driver->_counter_exhausted) {
        overflow = true;
    }
    if (driver->_device_generation == UINT32_MAX) {
        overflow = true;
    }
    if (bus_reset && driver->_bus_generation == UINT32_MAX) {
        overflow = true;
    }
    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        if (driver->_resource_generation[index] == UINT32_MAX) {
            overflow = true;
        }
    }
    if (overflow) {
        driver->_counter_exhausted = true;
        cf09_a_leave(driver);
        return CF09_A_RESULT_COUNTER_EXHAUSTED;
    }

    driver->_device_generation++;
    if (bus_reset) {
        driver->_bus_generation++;
    }
    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        driver->_resource_generation[index]++;
    }

    cf09_a_leave(driver);
    if (!backend_ok) {
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    if (quarantined) {
        return CF09_A_RESULT_UNCERTAIN;
    }
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_driver_init(
    cf09_a_driver_t *driver,
    cf09_a_session_id_t session,
    const cf09_a_backend_ops_t *backend)
{
    cf09_a_backend_ops_t selected_backend;

    if (driver == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_initialized) {
        return CF09_A_RESULT_INVALID_STATE;
    }
    if (backend == NULL || !cf09_a_session_valid(session)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    selected_backend = *backend;
    if (selected_backend.start_tx == NULL ||
        selected_backend.close_host_tx == NULL ||
        selected_backend.abort_host_tx == NULL ||
        selected_backend.set_outputs_enabled == NULL ||
        selected_backend.quiesce == NULL ||
        selected_backend.completion_health == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }

    memset(driver, 0, sizeof(*driver));
    driver->_initialized = true;
    driver->_mutating = true;
    driver->_session = session;
    driver->_backend = selected_backend;
    if (!driver->_backend.set_outputs_enabled(
            driver->_backend.user,
            false)) {
        driver->_backend_fault = true;
        driver->_mutating = false;
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    driver->_mutating = false;
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_driver_deinit(cf09_a_driver_t *driver)
{
    cf09_a_result_t entered;
    bool had_open = false;
    size_t index;

    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    if (!driver->_backend.set_outputs_enabled(
            driver->_backend.user,
            false)) {
        driver->_backend_fault = true;
        cf09_a_leave(driver);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    driver->_outputs_armed = false;
    if (driver->_host_pending) {
        had_open = true;
        if (!driver->_backend.abort_host_tx(
                driver->_backend.user,
                &driver->_host_pending_token)) {
            driver->_abort_pending = true;
            driver->_backend_fault = true;
        } else {
            cf09_a_clear_host_pending(driver);
        }
    }
    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        cf09_a_attempt_slot_t *slot = &driver->_slots[index];
        if (slot->occupied && slot->state != CF09_A_ATTEMPT_COMPLETE &&
            slot->state != CF09_A_ATTEMPT_EMPTY) {
            had_open = true;
            cf09_a_mark_slot_uncertain(driver, slot);
        }
    }
    if (driver->_abort_pending) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    if (!driver->_backend.quiesce(driver->_backend.user)) {
        driver->_backend_fault = true;
        cf09_a_leave(driver);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    memset(driver, 0, sizeof(*driver));
    if (had_open) {
        return CF09_A_RESULT_UNCERTAIN;
    }
    return CF09_A_RESULT_OK;
}

const cf09_a_route_t *cf09_a_route_for_logical(uint8_t logical_channel)
{
    size_t index;

    if (!cf09_a_logical_to_index(logical_channel, &index)) {
        return NULL;
    }
    return &CF09_A_ROUTES[index];
}

bool cf09_a_token_equal(
    const cf09_a_attempt_token_t *left,
    const cf09_a_attempt_token_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    return cf09_a_session_equal(left->session, right->session) &&
           left->frame.epoch == right->frame.epoch &&
           left->frame.sequence == right->frame.sequence &&
           left->attempt_no == right->attempt_no &&
           left->transfer_no == right->transfer_no &&
           left->logical_channel == right->logical_channel &&
           left->serializer_id == right->serializer_id &&
           left->resource_id == right->resource_id &&
           left->physical_channel == right->physical_channel &&
           left->device_id == right->device_id &&
           left->chip_select_id == right->chip_select_id &&
           left->resource_generation == right->resource_generation &&
           left->bus_generation == right->bus_generation &&
           left->device_generation == right->device_generation &&
           left->payload_len == right->payload_len &&
           left->payload_fingerprint == right->payload_fingerprint;
}

uint64_t cf09_a_payload_fingerprint(
    const uint8_t *payload,
    size_t payload_len)
{
    uint64_t value = UINT64_C(14695981039346656037);
    size_t index;

    if (!cf09_a_payload_valid(payload, payload_len)) {
        return 0U;
    }
    for (index = 0U; index < payload_len; ++index) {
        value ^= (uint64_t)payload[index];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

cf09_a_result_t cf09_a_confirm_bridge_up(cf09_a_driver_t *driver)
{
    cf09_a_result_t entered = cf09_a_enter(driver);

    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    if (driver->_reset_active || driver->_backend_fault ||
        driver->_counter_exhausted) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_NOT_READY;
    }
    driver->_bridge_up = true;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_release_reset(cf09_a_driver_t *driver)
{
    cf09_a_result_t entered = cf09_a_enter(driver);

    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    if (!driver->_reset_active) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_STATE;
    }
    driver->_reset_active = false;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_mark_resource_configured(
    cf09_a_driver_t *driver,
    uint8_t physical_channel,
    uint32_t generation)
{
    cf09_a_result_t entered = cf09_a_enter(driver);

    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    if (!cf09_a_physical_channel_valid(physical_channel)) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_outputs_armed) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_STATE;
    }
    if (!driver->_bridge_up || driver->_reset_active ||
        generation != driver->_resource_generation[physical_channel]) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_NOT_READY;
    }
    driver->_configured[physical_channel] = true;
    driver->_idle[physical_channel] = false;
    driver->_configured_generation[physical_channel] = generation;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_mark_resource_idle(
    cf09_a_driver_t *driver,
    uint8_t physical_channel,
    uint32_t generation)
{
    cf09_a_result_t entered = cf09_a_enter(driver);

    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    if (!cf09_a_physical_channel_valid(physical_channel)) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_outputs_armed) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_STATE;
    }
    if (!driver->_bridge_up || driver->_reset_active ||
        !driver->_configured[physical_channel] ||
        generation != driver->_resource_generation[physical_channel] ||
        generation != driver->_configured_generation[physical_channel]) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_NOT_READY;
    }
    driver->_idle[physical_channel] = true;
    driver->_idle_generation[physical_channel] = generation;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_arm_outputs(cf09_a_driver_t *driver)
{
    cf09_a_result_t entered = cf09_a_enter(driver);

    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    if (driver->_outputs_armed) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_STATE;
    }
    if (!driver->_bridge_up || driver->_reset_active ||
        driver->_backend_fault || driver->_counter_exhausted ||
        !cf09_a_all_resources_ready(driver) ||
        cf09_a_has_blocking_attempt(driver)) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_NOT_READY;
    }
    if (!driver->_backend.set_outputs_enabled(
            driver->_backend.user,
            true)) {
        cf09_a_fail_closed(driver, NULL, true);
        cf09_a_leave(driver);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    driver->_outputs_armed = true;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_begin_tx(
    cf09_a_driver_t *driver,
    const cf09_a_tx_request_t *request,
    cf09_a_attempt_token_t *out_token)
{
    cf09_a_attempt_token_t candidate;
    cf09_a_attempt_slot_t *slot;
    const cf09_a_route_t *route;
    cf09_a_result_t result;
    size_t index;

    if (out_token != NULL) {
        memset(out_token, 0, sizeof(*out_token));
    }
    if (driver == NULL || request == NULL || out_token == NULL ||
        !cf09_a_payload_valid(request->payload, request->payload_len) ||
        !cf09_a_logical_to_index(request->logical_channel, &index)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    result = cf09_a_enter(driver);
    if (result != CF09_A_RESULT_OK) {
        return result;
    }
    if (!cf09_a_tx_ready(driver)) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_NOT_READY;
    }
    if (driver->_host_pending) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_BUSY;
    }
    slot = &driver->_slots[index];
    if (slot->occupied) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_BUSY;
    }
    if (!cf09_a_frame_is_newer(driver, index, request->frame)) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_last_transfer_no == UINT64_MAX) {
        driver->_counter_exhausted = true;
        cf09_a_fail_closed(driver, NULL, false);
        cf09_a_leave(driver);
        return CF09_A_RESULT_COUNTER_EXHAUSTED;
    }

    route = &CF09_A_ROUTES[index];
    memset(&candidate, 0, sizeof(candidate));
    candidate.session = driver->_session;
    candidate.frame = request->frame;
    candidate.attempt_no = 1U;
    candidate.transfer_no = driver->_last_transfer_no + 1U;
    candidate.logical_channel = route->logical_channel;
    candidate.serializer_id = route->serializer_id;
    candidate.resource_id = route->resource_id;
    candidate.physical_channel = route->physical_channel;
    candidate.device_id = (uint8_t)CF09_A_DEVICE_ID;
    candidate.chip_select_id = (uint8_t)CF09_A_CHIP_SELECT_ID;
    candidate.resource_generation = driver->_resource_generation[index];
    candidate.bus_generation = driver->_bus_generation;
    candidate.device_generation = driver->_device_generation;
    candidate.payload_len = request->payload_len;
    candidate.payload_fingerprint = cf09_a_payload_fingerprint(
        request->payload,
        request->payload_len);

    result = cf09_a_start_candidate(
        driver,
        slot,
        &candidate,
        request->payload,
        request->payload_len,
        out_token);
    if (out_token->transfer_no != 0U) {
        driver->_frame_seen[index] = true;
        driver->_last_frame[index] = request->frame;
    }
    cf09_a_leave(driver);
    return result;
}

cf09_a_result_t cf09_a_retry_not_committed(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *previous_token,
    const uint8_t *payload,
    size_t payload_len,
    cf09_a_attempt_token_t *out_token)
{
    cf09_a_attempt_token_t candidate;
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t result;

    if (out_token != NULL) {
        memset(out_token, 0, sizeof(*out_token));
    }
    if (driver == NULL || previous_token == NULL || out_token == NULL ||
        !cf09_a_payload_valid(payload, payload_len)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    result = cf09_a_enter(driver);
    if (result != CF09_A_RESULT_OK) {
        return result;
    }
    slot = cf09_a_current_slot(driver, previous_token, true, NULL);
    if (slot == NULL) {
        result = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return result;
    }
    if (slot->state != CF09_A_ATTEMPT_NOT_COMMITTED) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_STATE;
    }
    if (!cf09_a_tx_ready(driver) || driver->_host_pending) {
        cf09_a_leave(driver);
        return driver->_host_pending ? CF09_A_RESULT_BUSY :
                                       CF09_A_RESULT_NOT_READY;
    }
    if (payload_len != previous_token->payload_len ||
        cf09_a_payload_fingerprint(payload, payload_len) !=
            previous_token->payload_fingerprint) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (previous_token->attempt_no == UINT32_MAX ||
        driver->_last_transfer_no == UINT64_MAX) {
        driver->_counter_exhausted = true;
        cf09_a_fail_closed(driver, NULL, false);
        cf09_a_leave(driver);
        return CF09_A_RESULT_COUNTER_EXHAUSTED;
    }

    candidate = *previous_token;
    candidate.attempt_no++;
    candidate.transfer_no = driver->_last_transfer_no + 1U;
    result = cf09_a_start_candidate(
        driver,
        slot,
        &candidate,
        payload,
        payload_len,
        out_token);
    cf09_a_leave(driver);
    return result;
}

cf09_a_result_t cf09_a_on_host_result(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL ||
        (result != CF09_A_COMMIT_COMMITTED &&
         result != CF09_A_COMMIT_NOT_COMMITTED &&
         result != CF09_A_COMMIT_UNKNOWN)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, true, NULL);
    if (slot == NULL || slot->state != CF09_A_ATTEMPT_HOST_PENDING ||
        !driver->_host_pending ||
        !cf09_a_token_equal(&driver->_host_pending_token, token)) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }

    if (!driver->_backend.close_host_tx(driver->_backend.user, token)) {
        cf09_a_fail_closed(driver, token, true);
        cf09_a_leave(driver);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    cf09_a_clear_host_pending(driver);

    if (result == CF09_A_COMMIT_NOT_COMMITTED) {
        slot->state = CF09_A_ATTEMPT_NOT_COMMITTED;
        cf09_a_leave(driver);
        return CF09_A_RESULT_NOT_COMMITTED;
    }
    if (result == CF09_A_COMMIT_UNKNOWN) {
        cf09_a_mark_slot_uncertain(driver, slot);
        if (driver->_counter_exhausted) {
            cf09_a_fail_closed(driver, NULL, false);
            cf09_a_leave(driver);
            return CF09_A_RESULT_COUNTER_EXHAUSTED;
        }
        cf09_a_leave(driver);
        return CF09_A_RESULT_UNCERTAIN;
    }

    slot->state = CF09_A_ATTEMPT_HOST_COMMITTED;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_discard_not_committed(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, true, NULL);
    if (slot == NULL) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }
    if (slot->state != CF09_A_ATTEMPT_NOT_COMMITTED) {
        cf09_a_leave(driver);
        return CF09_A_RESULT_INVALID_STATE;
    }
    memset(slot, 0, sizeof(*slot));
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_on_serializer_done_latched(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, true, NULL);
    if (slot == NULL || slot->state != CF09_A_ATTEMPT_HOST_COMMITTED) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }
    if (!cf09_a_tx_ready(driver)) {
        cf09_a_mark_slot_uncertain(driver, slot);
        if (driver->_counter_exhausted) {
            cf09_a_fail_closed(driver, NULL, false);
            cf09_a_leave(driver);
            return CF09_A_RESULT_COUNTER_EXHAUSTED;
        }
        cf09_a_leave(driver);
        return CF09_A_RESULT_UNCERTAIN;
    }
    slot->state = CF09_A_ATTEMPT_SERIALIZER_DONE_LATCHED;
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_confirm_serializer_complete(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t source)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL ||
        (source != CF09_A_COMPLETION_IRQ &&
         source != CF09_A_COMPLETION_STATUS_POLL)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, true, NULL);
    if (slot == NULL ||
        slot->state != CF09_A_ATTEMPT_SERIALIZER_DONE_LATCHED) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }
    if (!cf09_a_tx_ready(driver)) {
        cf09_a_mark_slot_uncertain(driver, slot);
        if (driver->_counter_exhausted) {
            cf09_a_fail_closed(driver, NULL, false);
            cf09_a_leave(driver);
            return CF09_A_RESULT_COUNTER_EXHAUSTED;
        }
        cf09_a_leave(driver);
        return CF09_A_RESULT_UNCERTAIN;
    }
    if (driver->_completed_count == UINT64_MAX) {
        driver->_counter_exhausted = true;
        cf09_a_fail_closed(driver, NULL, false);
        cf09_a_leave(driver);
        return CF09_A_RESULT_COUNTER_EXHAUSTED;
    }
    slot->state = CF09_A_ATTEMPT_COMPLETE;
    driver->_completed_count++;
    driver->_completed_route_mask = (uint8_t)(
        driver->_completed_route_mask |
        (uint8_t)(1U << token->physical_channel));
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_on_completion_timeout(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, true, NULL);
    if (slot == NULL ||
        (slot->state != CF09_A_ATTEMPT_HOST_COMMITTED &&
         slot->state != CF09_A_ATTEMPT_SERIALIZER_DONE_LATCHED)) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }
    cf09_a_mark_slot_uncertain(driver, slot);
    if (driver->_counter_exhausted) {
        cf09_a_fail_closed(driver, NULL, false);
        cf09_a_leave(driver);
        return CF09_A_RESULT_COUNTER_EXHAUSTED;
    }
    cf09_a_leave(driver);
    return CF09_A_RESULT_UNCERTAIN;
}

cf09_a_result_t cf09_a_on_device_reset(cf09_a_driver_t *driver)
{
    return cf09_a_reset_common(driver, false);
}

cf09_a_result_t cf09_a_on_bus_reset(cf09_a_driver_t *driver)
{
    return cf09_a_reset_common(driver, true);
}

cf09_a_result_t cf09_a_reap_complete(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, false, NULL);
    if (slot == NULL || slot->state != CF09_A_ATTEMPT_COMPLETE) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }
    memset(slot, 0, sizeof(*slot));
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_reconcile_uncertain(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_attempt_slot_t *slot;
    cf09_a_result_t entered;

    if (token == NULL) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    entered = cf09_a_enter(driver);
    if (entered != CF09_A_RESULT_OK) {
        return entered;
    }
    slot = cf09_a_current_slot(driver, token, false, NULL);
    if (slot == NULL || slot->state != CF09_A_ATTEMPT_UNCERTAIN) {
        entered = cf09_a_reject_callback(driver);
        cf09_a_leave(driver);
        return entered;
    }
    memset(slot, 0, sizeof(*slot));
    cf09_a_leave(driver);
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_get_attempt_state(
    const cf09_a_driver_t *driver,
    uint8_t logical_channel,
    cf09_a_attempt_state_t *out_state)
{
    size_t index;

    if (out_state != NULL) {
        *out_state = CF09_A_ATTEMPT_EMPTY;
    }
    if (driver == NULL || !driver->_initialized || out_state == NULL ||
        !cf09_a_logical_to_index(logical_channel, &index)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    *out_state = driver->_slots[index].occupied ?
        driver->_slots[index].state : CF09_A_ATTEMPT_EMPTY;
    return CF09_A_RESULT_OK;
}

cf09_a_result_t cf09_a_get_resource_generation(
    const cf09_a_driver_t *driver,
    uint8_t physical_channel,
    uint32_t *out_generation)
{
    if (out_generation != NULL) {
        *out_generation = 0U;
    }
    if (driver == NULL || !driver->_initialized || out_generation == NULL ||
        !cf09_a_physical_channel_valid(physical_channel)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    *out_generation = driver->_resource_generation[physical_channel];
    return CF09_A_RESULT_OK;
}

cf09_a_validation_t cf09_a_validate(const cf09_a_driver_t *driver)
{
    cf09_a_validation_t validation;
    size_t host_pending_slots = 0U;
    bool invariant_error = false;
    size_t index;

    memset(&validation, 0, sizeof(validation));
    validation.model_status = CF09_A_MODEL_ERROR;
    validation.model_class = CF09_A_MODEL_CLASS;
    validation.hardware_status = CF09_A_HARDWARE_STATUS;
    validation.exact_part_status = CF09_A_EXACT_PART_STATUS;
    validation.connector_delivery_status = CF09_A_CONNECTOR_STATUS;
    validation.completion_scope = CF09_A_COMPLETION_SCOPE;

    if (driver == NULL || !driver->_initialized) {
        return validation;
    }

    validation.completed_count = driver->_completed_count;
    validation.completed_route_mask = driver->_completed_route_mask;
    validation.uncertain_count = driver->_uncertain_count;
    validation.rejected_callback_count = driver->_rejected_callback_count;
    validation.outputs_armed = driver->_outputs_armed;
    validation.bridge_up = driver->_bridge_up;
    validation.reset_active = driver->_reset_active;
    validation.completion_path_health =
        driver->_backend.completion_health(driver->_backend.user);

    for (index = 0U; index < (size_t)CF09_A_EXTERNAL_TX_COUNT; ++index) {
        const cf09_a_attempt_slot_t *slot = &driver->_slots[index];
        size_t token_index = 0U;

        if (!slot->occupied) {
            if (slot->state != CF09_A_ATTEMPT_EMPTY) {
                invariant_error = true;
            }
            continue;
        }
        if (slot->state == CF09_A_ATTEMPT_EMPTY ||
            !cf09_a_token_route_valid(driver, &slot->token, &token_index) ||
            token_index != index) {
            invariant_error = true;
        }
        if (slot->state == CF09_A_ATTEMPT_HOST_PENDING) {
            host_pending_slots++;
            validation.open_attempts++;
        } else if (slot->state == CF09_A_ATTEMPT_NOT_COMMITTED ||
                   slot->state == CF09_A_ATTEMPT_HOST_COMMITTED ||
                   slot->state == CF09_A_ATTEMPT_SERIALIZER_DONE_LATCHED) {
            validation.open_attempts++;
        } else if (slot->state == CF09_A_ATTEMPT_UNCERTAIN) {
            validation.uncertain_attempts++;
        } else if (slot->state != CF09_A_ATTEMPT_COMPLETE) {
            invariant_error = true;
        }
    }

    if (driver->_abort_pending) {
        size_t pending_index = 0U;

        if (!driver->_host_pending ||
            !cf09_a_token_route_valid(
                driver,
                &driver->_host_pending_token,
                &pending_index)) {
            invariant_error = true;
        }
        validation.open_attempts++;
    } else if (driver->_host_pending) {
        if (host_pending_slots != 1U) {
            invariant_error = true;
        } else {
            size_t pending_index = 0U;
            const cf09_a_attempt_slot_t *slot = NULL;

            if (cf09_a_token_route_valid(
                    driver,
                    &driver->_host_pending_token,
                    &pending_index)) {
                slot = &driver->_slots[pending_index];
            }
            if (slot == NULL || !slot->occupied ||
                slot->state != CF09_A_ATTEMPT_HOST_PENDING ||
                !cf09_a_token_equal(
                    &slot->token,
                    &driver->_host_pending_token) ||
                !cf09_a_token_generations_current(
                    driver,
                    &driver->_host_pending_token,
                    pending_index)) {
                invariant_error = true;
            }
        }
    } else if (host_pending_slots != 0U) {
        invariant_error = true;
    }

    if ((driver->_completed_route_mask &
         (uint8_t)(~(uint8_t)CF09_A_EXTERNAL_TX_COMPLETE_MASK)) != 0U) {
        invariant_error = true;
    }

    if (driver->_outputs_armed &&
        (!driver->_bridge_up || driver->_reset_active ||
         !cf09_a_all_resources_ready(driver))) {
        invariant_error = true;
    }

    if (invariant_error || driver->_backend_fault ||
        driver->_counter_exhausted) {
        validation.model_status = CF09_A_MODEL_ERROR;
    } else if (driver->_uncertain_count != 0U ||
               validation.uncertain_attempts != 0U) {
        validation.model_status = CF09_A_MODEL_UNCERTAIN;
    } else if (validation.open_attempts != 0U) {
        validation.model_status = CF09_A_MODEL_NOT_READY;
    } else if (driver->_completed_count == 0U) {
        validation.model_status = CF09_A_MODEL_NOT_RUN;
    } else if (driver->_completed_route_mask !=
                   (uint8_t)CF09_A_EXTERNAL_TX_COMPLETE_MASK ||
               validation.completion_path_health !=
                   CF09_A_COMPLETION_PATH_HEALTHY ||
               !driver->_outputs_armed || !driver->_bridge_up ||
               driver->_reset_active) {
        validation.model_status = CF09_A_MODEL_NOT_READY;
    } else {
        validation.model_status = CF09_A_MODEL_PASS;
    }
    return validation;
}

const char *cf09_a_result_label(cf09_a_result_t result)
{
    switch (result) {
    case CF09_A_RESULT_OK:
        return "OK";
    case CF09_A_RESULT_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case CF09_A_RESULT_INVALID_STATE:
        return "INVALID_STATE";
    case CF09_A_RESULT_NOT_READY:
        return "NOT_READY";
    case CF09_A_RESULT_BUSY:
        return "BUSY";
    case CF09_A_RESULT_BACKEND_REJECTED:
        return "BACKEND_REJECTED";
    case CF09_A_RESULT_BACKEND_FAILURE:
        return "BACKEND_FAILURE";
    case CF09_A_RESULT_STALE_TOKEN:
        return "STALE_TOKEN";
    case CF09_A_RESULT_NOT_COMMITTED:
        return "NOT_COMMITTED";
    case CF09_A_RESULT_UNCERTAIN:
        return "UNCERTAIN";
    case CF09_A_RESULT_COUNTER_EXHAUSTED:
        return "COUNTER_EXHAUSTED";
    case CF09_A_RESULT_REENTRANT:
        return "REENTRANT";
    default:
        return "INVALID_ENUM";
    }
}

const char *cf09_a_model_status_label(cf09_a_model_status_t status)
{
    switch (status) {
    case CF09_A_MODEL_NOT_RUN:
        return "A_SINGLE_QUAD_C_MODEL_NOT_RUN";
    case CF09_A_MODEL_NOT_READY:
        return "A_SINGLE_QUAD_C_MODEL_NOT_READY";
    case CF09_A_MODEL_PASS:
        return "A_SINGLE_QUAD_C_MODEL_PASS";
    case CF09_A_MODEL_UNCERTAIN:
        return "A_SINGLE_QUAD_C_MODEL_UNCERTAIN";
    case CF09_A_MODEL_ERROR:
        return "A_SINGLE_QUAD_C_MODEL_ERROR";
    default:
        return "A_SINGLE_QUAD_C_MODEL_INVALID_ENUM";
    }
}
