#include "cf09_max14830_guard_scheduler_v1.h"

#include <limits.h>
#include <string.h>

static void cf09_max14830_guard_count(uint64_t *counter)
{
    if (counter != NULL && *counter != UINT64_MAX) {
        (*counter)++;
    }
}

static bool cf09_max14830_guard_uart_valid(uint8_t uart)
{
    return uart < (uint8_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS;
}

static bool cf09_max14830_guard_event_equal(
    const cf09_max14830_guard_event_t *left,
    const cf09_max14830_guard_event_t *right)
{
    return left != NULL && right != NULL &&
           left->uart == right->uart &&
           left->arm_generation == right->arm_generation &&
           left->alarm_value == right->alarm_value;
}

static void cf09_max14830_guard_clear_slot(
    cf09_max14830_guard_slot_t *slot)
{
    if (slot != NULL) {
        memset(slot, 0, sizeof(*slot));
    }
}

static void cf09_max14830_guard_retire_cancelled_slot(
    cf09_max14830_guard_slot_t *slot)
{
    cf09_a_attempt_token_t token;
    bool token_valid;

    if (slot == NULL) {
        return;
    }
    token_valid = slot->binding.valid;
    if (token_valid) {
        token = slot->binding.token;
    } else {
        memset(&token, 0, sizeof(token));
    }
    cf09_max14830_guard_clear_slot(slot);
    if (token_valid) {
        slot->cancellation_tombstone = token;
        slot->cancellation_tombstone_valid = true;
    }
}

static void cf09_max14830_guard_fault_slot(
    cf09_max14830_guard_scheduler_t *scheduler,
    cf09_max14830_guard_slot_t *slot)
{
    if (slot != NULL) {
        slot->state = CF09_MAX14830_GUARD_SLOT_FAULTED;
    }
    if (scheduler != NULL) {
        cf09_max14830_guard_count(&scheduler->_fault_count);
    }
}

static cf09_max14830_guard_result_t cf09_max14830_guard_enter(
    cf09_max14830_guard_scheduler_t *scheduler)
{
    if (scheduler == NULL || !scheduler->_initialized) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    if (scheduler->_mutating) {
        return CF09_MAX14830_GUARD_BUSY;
    }
    scheduler->_mutating = true;
    return CF09_MAX14830_GUARD_OK;
}

static void cf09_max14830_guard_leave(
    cf09_max14830_guard_scheduler_t *scheduler)
{
    if (scheduler != NULL) {
        scheduler->_mutating = false;
    }
}

static bool cf09_max14830_guard_read_configuration(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    uint32_t *generation_out)
{
    if (scheduler == NULL || generation_out == NULL ||
        scheduler->_platform.read_configuration_generation == NULL) {
        return false;
    }
    *generation_out = 0U;
    return scheduler->_platform.read_configuration_generation(
               scheduler->_platform.user, uart, generation_out) &&
           *generation_out != 0U && *generation_out != UINT32_MAX;
}

static bool cf09_max14830_guard_read_timer(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    const cf09_max14830_guard_slot_t *slot,
    cf09_max14830_guard_timer_snapshot_t *snapshot_out)
{
    if (scheduler == NULL || slot == NULL || snapshot_out == NULL ||
        scheduler->_platform.read_timer == NULL) {
        return false;
    }
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    if (!scheduler->_platform.read_timer(
            scheduler->_platform.user, uart, snapshot_out)) {
        return false;
    }
    return snapshot_out->timer_hz_upper_bound != 0U &&
           snapshot_out->timer_generation != 0U &&
           snapshot_out->timer_generation != UINT32_MAX &&
           snapshot_out->minimum_reliable_delay_ticks != 0U &&
           slot->derived.config_snapshot.timer_hz_upper_bound != 0U &&
           snapshot_out->timer_hz_upper_bound <=
               slot->derived.config_snapshot.timer_hz_upper_bound;
}

static bool cf09_max14830_guard_slot_token_equal(
    const cf09_max14830_guard_slot_t *slot,
    const cf09_a_attempt_token_t *token)
{
    return slot != NULL && token != NULL && slot->binding.valid &&
           cf09_a_token_equal(&slot->binding.token, token);
}

static void cf09_max14830_guard_quarantine_fault(
    cf09_max14830_guard_scheduler_t *scheduler,
    cf09_max14830_guard_slot_t *slot)
{
    cf09_max14830_guard_event_t alarm;

    if (scheduler == NULL || slot == NULL) {
        return;
    }
    alarm = slot->alarm;
    slot->state = CF09_MAX14830_GUARD_SLOT_QUARANTINED;
    cf09_max14830_guard_count(&scheduler->_fault_count);
    if (scheduler->_platform.cancel_absolute(
            scheduler->_platform.user, &alarm)) {
        slot->state = CF09_MAX14830_GUARD_SLOT_FAULTED;
    }
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_init(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_guard_platform_ops_t *platform)
{
    cf09_max14830_guard_platform_ops_t platform_copy;

    if (scheduler == NULL || platform == NULL) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    if (scheduler->_initialized) {
        return CF09_MAX14830_GUARD_INVALID_STATE;
    }
    platform_copy = *platform;
    if (platform_copy.read_timer == NULL ||
        platform_copy.read_configuration_generation == NULL ||
        platform_copy.arm_absolute == NULL ||
        platform_copy.cancel_absolute == NULL) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->_platform = platform_copy;
    scheduler->_next_arm_generation = UINT64_C(1);
    scheduler->_initialized = true;
    return CF09_MAX14830_GUARD_OK;
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_admit(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_timing_derived_t *derived,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_timing_derived_t derived_copy;
    cf09_max14830_timing_config_t config_copy;
    cf09_a_attempt_token_t token_copy;
    cf09_max14830_timing_binding_t binding;
    cf09_max14830_guard_slot_t *slot;
    uint32_t configuration_generation;
    uint8_t uart;

    if (derived == NULL || token == NULL) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    config_copy = derived->config_snapshot;
    token_copy = *token;
    if (cf09_max14830_timing_derive_guard(
            &config_copy, &derived_copy) != CF09_MAX14830_TIMING_OK) {
        return CF09_MAX14830_GUARD_TIMING_PLAN_INVALID;
    }
    uart = derived_copy.config_snapshot.uart;
    if (!cf09_max14830_guard_uart_valid(uart) ||
        derived_copy.guard_ticks_minimum == 0U) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    slot = &scheduler->_slots[uart];
    if (slot->state != CF09_MAX14830_GUARD_SLOT_IDLE) {
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_BUSY;
    }
    if (!cf09_max14830_guard_read_configuration(
            scheduler, uart, &configuration_generation)) {
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_FAILURE;
    }
    memset(&binding, 0, sizeof(binding));
    if (!cf09_max14830_timing_bind_attempt(
            &derived_copy,
            &token_copy,
            configuration_generation,
            &binding)) {
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_BINDING_MISMATCH;
    }
    cf09_max14830_guard_clear_slot(slot);
    slot->derived = derived_copy;
    slot->binding = binding;
    memset(&slot->alarm, 0, sizeof(slot->alarm));
    slot->state = CF09_MAX14830_GUARD_SLOT_ADMITTED;
    cf09_max14830_guard_count(&scheduler->_admitted_count);
    cf09_max14830_guard_leave(scheduler);
    return CF09_MAX14830_GUARD_OK;
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_arm(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    const cf09_a_attempt_token_t *token,
    cf09_max14830_guard_event_t *event_out)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_guard_slot_t *slot;
    cf09_max14830_guard_timer_snapshot_t timer;
    cf09_max14830_guard_event_t event;
    cf09_max14830_guard_timer_arm_result_t arm_result;
    cf09_a_attempt_token_t token_copy;
    uint32_t configuration_generation;
    uint64_t delay_ticks;

    if (!cf09_max14830_guard_uart_valid(uart) || token == NULL ||
        event_out == NULL) {
        if (event_out != NULL) {
            memset(event_out, 0, sizeof(*event_out));
        }
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    token_copy = *token;
    memset(event_out, 0, sizeof(*event_out));
    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    slot = &scheduler->_slots[uart];
    if (slot->state != CF09_MAX14830_GUARD_SLOT_ADMITTED) {
        cf09_max14830_guard_leave(scheduler);
        return slot->state == CF09_MAX14830_GUARD_SLOT_ARMED ?
            CF09_MAX14830_GUARD_BUSY :
            CF09_MAX14830_GUARD_INVALID_STATE;
    }
    if (!cf09_max14830_guard_slot_token_equal(slot, &token_copy)) {
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_STALE_TOKEN;
    }
    if (!cf09_max14830_guard_read_configuration(
            scheduler, uart, &configuration_generation)) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_FAILURE;
    }
    if (!cf09_max14830_timing_binding_matches(
            &slot->derived,
            &slot->binding,
            &token_copy,
            configuration_generation)) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_BINDING_MISMATCH;
    }
    if (!cf09_max14830_guard_read_timer(
            scheduler, uart, slot, &timer)) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID;
    }
    delay_ticks = slot->derived.guard_ticks_minimum;
    if (delay_ticks < timer.minimum_reliable_delay_ticks) {
        delay_ticks = timer.minimum_reliable_delay_ticks;
    }
    if (timer.now_ticks > UINT64_MAX - delay_ticks) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_ARITHMETIC_OVERFLOW;
    }
    if (scheduler->_next_arm_generation == 0U ||
        scheduler->_next_arm_generation == UINT64_MAX) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_COUNTER_EXHAUSTED;
    }
    memset(&event, 0, sizeof(event));
    event.uart = uart;
    event.arm_generation = scheduler->_next_arm_generation;
    event.alarm_value = timer.now_ticks + delay_ticks;
    scheduler->_next_arm_generation++;

    /* Publish identity before the port call; inline re-entry is still BUSY. */
    slot->alarm = event;
    slot->timer_generation = timer.timer_generation;
    slot->scheduled_delay_ticks = delay_ticks;
    slot->state = CF09_MAX14830_GUARD_SLOT_ARMED;
    arm_result = scheduler->_platform.arm_absolute(
        scheduler->_platform.user, &event);
    if (arm_result == CF09_MAX14830_GUARD_TIMER_ARM_ACCEPTED) {
        cf09_max14830_guard_count(&scheduler->_armed_count);
        *event_out = event;
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_OK;
    }
    if (arm_result ==
            CF09_MAX14830_GUARD_TIMER_ARM_REJECTED_NO_EFFECT) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_FAILURE;
    }

    /* Unknown or invalid result: invalidate before best-effort cancellation. */
    cf09_max14830_guard_quarantine_fault(scheduler, slot);
    cf09_max14830_guard_leave(scheduler);
    return CF09_MAX14830_GUARD_TIMER_ACCEPTANCE_UNKNOWN;
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_consume_event(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_guard_event_t *event,
    const cf09_a_attempt_token_t *current_token,
    cf09_a_attempt_token_t *token_out)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_guard_slot_t *slot;
    cf09_max14830_guard_timer_snapshot_t timer;
    uint32_t configuration_generation;
    cf09_a_attempt_token_t released_token;
    cf09_a_attempt_token_t current_token_copy;
    cf09_max14830_guard_event_t event_copy;

    if (event == NULL || current_token == NULL || token_out == NULL) {
        if (token_out != NULL) {
            memset(token_out, 0, sizeof(*token_out));
        }
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    event_copy = *event;
    current_token_copy = *current_token;
    memset(token_out, 0, sizeof(*token_out));
    if (!cf09_max14830_guard_uart_valid(event_copy.uart) ||
        event_copy.arm_generation == 0U) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    slot = &scheduler->_slots[event_copy.uart];
    if (slot->state != CF09_MAX14830_GUARD_SLOT_ARMED ||
        !cf09_max14830_guard_event_equal(&slot->alarm, &event_copy)) {
        cf09_max14830_guard_count(&scheduler->_stale_event_count);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_STALE_EVENT;
    }
    if (!cf09_max14830_guard_read_configuration(
            scheduler, event_copy.uart, &configuration_generation)) {
        cf09_max14830_guard_quarantine_fault(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_FAILURE;
    }
    if (!cf09_max14830_guard_read_timer(
            scheduler, event_copy.uart, slot, &timer)) {
        cf09_max14830_guard_quarantine_fault(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID;
    }
    if (timer.timer_generation != slot->timer_generation ||
        timer.minimum_reliable_delay_ticks >
            slot->scheduled_delay_ticks) {
        cf09_max14830_guard_quarantine_fault(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID;
    }
    if (timer.now_ticks < event_copy.alarm_value) {
        cf09_max14830_guard_quarantine_fault(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_EARLY_EVENT;
    }
    if (!cf09_max14830_timing_binding_matches(
            &slot->derived,
            &slot->binding,
            &current_token_copy,
            configuration_generation)) {
        cf09_max14830_guard_quarantine_fault(scheduler, slot);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_BINDING_MISMATCH;
    }
    released_token = slot->binding.token;
    cf09_max14830_guard_clear_slot(slot);
    cf09_max14830_guard_count(&scheduler->_consumed_count);
    *token_out = released_token;
    cf09_max14830_guard_leave(scheduler);
    return CF09_MAX14830_GUARD_OK;
}

cf09_max14830_guard_result_t
cf09_max14830_guard_scheduler_report_delivery_failure(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_guard_event_t *event)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_guard_event_t event_copy;
    cf09_max14830_guard_slot_t *slot;

    if (event == NULL) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    event_copy = *event;
    if (!cf09_max14830_guard_uart_valid(event_copy.uart) ||
        event_copy.arm_generation == 0U) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    slot = &scheduler->_slots[event_copy.uart];
    if (slot->state != CF09_MAX14830_GUARD_SLOT_ARMED ||
        !cf09_max14830_guard_event_equal(&slot->alarm, &event_copy)) {
        cf09_max14830_guard_count(&scheduler->_stale_event_count);
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_STALE_EVENT;
    }
    cf09_max14830_guard_quarantine_fault(scheduler, slot);
    cf09_max14830_guard_leave(scheduler);
    return CF09_MAX14830_GUARD_TIMER_FAILURE;
}

static cf09_max14830_guard_result_t
cf09_max14830_guard_cancel_slot(
    cf09_max14830_guard_scheduler_t *scheduler,
    cf09_max14830_guard_slot_t *slot)
{
    cf09_max14830_guard_event_t alarm;

    if (scheduler == NULL || slot == NULL) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    if (slot->state == CF09_MAX14830_GUARD_SLOT_IDLE) {
        return CF09_MAX14830_GUARD_OK;
    }
    if (slot->state == CF09_MAX14830_GUARD_SLOT_ADMITTED ||
        slot->state == CF09_MAX14830_GUARD_SLOT_FAULTED) {
        cf09_max14830_guard_retire_cancelled_slot(slot);
        return CF09_MAX14830_GUARD_OK;
    }
    if (slot->state != CF09_MAX14830_GUARD_SLOT_ARMED &&
        slot->state != CF09_MAX14830_GUARD_SLOT_QUARANTINED) {
        cf09_max14830_guard_fault_slot(scheduler, slot);
        return CF09_MAX14830_GUARD_INVALID_STATE;
    }
    alarm = slot->alarm;
    /* Invalidate before cancellation so an in-flight/queued event is stale. */
    slot->state = CF09_MAX14830_GUARD_SLOT_QUARANTINED;
    if (!scheduler->_platform.cancel_absolute(
            scheduler->_platform.user, &alarm)) {
        cf09_max14830_guard_count(&scheduler->_fault_count);
        return CF09_MAX14830_GUARD_TIMER_FAILURE;
    }
    cf09_max14830_guard_retire_cancelled_slot(slot);
    return CF09_MAX14830_GUARD_OK;
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_cancel(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_guard_result_t result;
    cf09_max14830_guard_slot_t *slot;

    if (!cf09_max14830_guard_uart_valid(uart) || token == NULL) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    slot = &scheduler->_slots[uart];
    if (slot->state == CF09_MAX14830_GUARD_SLOT_IDLE) {
        result = slot->cancellation_tombstone_valid &&
                cf09_a_token_equal(
                    &slot->cancellation_tombstone, token) ?
            CF09_MAX14830_GUARD_OK : CF09_MAX14830_GUARD_STALE_TOKEN;
        cf09_max14830_guard_leave(scheduler);
        return result;
    }
    if (!cf09_max14830_guard_slot_token_equal(slot, token)) {
        cf09_max14830_guard_leave(scheduler);
        return CF09_MAX14830_GUARD_STALE_TOKEN;
    }
    result = cf09_max14830_guard_cancel_slot(scheduler, slot);
    cf09_max14830_guard_leave(scheduler);
    return result;
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_invalidate(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_guard_result_t result;

    if (!cf09_max14830_guard_uart_valid(uart)) {
        return CF09_MAX14830_GUARD_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    result = cf09_max14830_guard_cancel_slot(
        scheduler, &scheduler->_slots[uart]);
    cf09_max14830_guard_leave(scheduler);
    return result;
}

cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_invalidate_all(
    cf09_max14830_guard_scheduler_t *scheduler)
{
    cf09_max14830_guard_result_t entered;
    cf09_max14830_guard_result_t result = CF09_MAX14830_GUARD_OK;
    cf09_max14830_guard_result_t current;
    size_t index;

    entered = cf09_max14830_guard_enter(scheduler);
    if (entered != CF09_MAX14830_GUARD_OK) {
        return entered;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS;
         ++index) {
        current = cf09_max14830_guard_cancel_slot(
            scheduler, &scheduler->_slots[index]);
        if (current != CF09_MAX14830_GUARD_OK &&
            result == CF09_MAX14830_GUARD_OK) {
            result = current;
        }
    }
    cf09_max14830_guard_leave(scheduler);
    return result;
}

cf09_max14830_guard_slot_state_t cf09_max14830_guard_scheduler_slot_state(
    const cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart)
{
    if (scheduler == NULL || !scheduler->_initialized ||
        !cf09_max14830_guard_uart_valid(uart)) {
        return CF09_MAX14830_GUARD_SLOT_FAULTED;
    }
    return scheduler->_slots[uart].state;
}

cf09_max14830_guard_validation_t cf09_max14830_guard_scheduler_validate(
    const cf09_max14830_guard_scheduler_t *scheduler)
{
    cf09_max14830_guard_validation_t validation;
    size_t index;

    memset(&validation, 0, sizeof(validation));
    validation.model_status = CF09_MAX14830_GUARD_SCHEDULER_MODEL_STATUS;
    validation.integration_status =
        CF09_MAX14830_GUARD_SCHEDULER_INTEGRATION_STATUS;
    validation.isr_policy = CF09_MAX14830_GUARD_SCHEDULER_ISR_POLICY;
    if (scheduler == NULL) {
        return validation;
    }
    validation.initialized = scheduler->_initialized;
    validation.mutating = scheduler->_mutating;
    validation.next_arm_generation = scheduler->_next_arm_generation;
    for (index = 0U;
         index < (size_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS;
         ++index) {
        validation.slot_state[index] = scheduler->_slots[index].state;
        validation.cancellation_tombstone_valid[index] =
            scheduler->_slots[index].cancellation_tombstone_valid;
    }
    validation.admitted_count = scheduler->_admitted_count;
    validation.armed_count = scheduler->_armed_count;
    validation.consumed_count = scheduler->_consumed_count;
    validation.stale_event_count = scheduler->_stale_event_count;
    validation.fault_count = scheduler->_fault_count;
    return validation;
}

const char *cf09_max14830_guard_result_name(
    cf09_max14830_guard_result_t result)
{
    switch (result) {
    case CF09_MAX14830_GUARD_OK:
        return "OK";
    case CF09_MAX14830_GUARD_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case CF09_MAX14830_GUARD_INVALID_STATE:
        return "INVALID_STATE";
    case CF09_MAX14830_GUARD_BUSY:
        return "BUSY";
    case CF09_MAX14830_GUARD_STALE_TOKEN:
        return "STALE_TOKEN";
    case CF09_MAX14830_GUARD_STALE_EVENT:
        return "STALE_EVENT";
    case CF09_MAX14830_GUARD_BINDING_MISMATCH:
        return "BINDING_MISMATCH";
    case CF09_MAX14830_GUARD_TIMING_PLAN_INVALID:
        return "TIMING_PLAN_INVALID";
    case CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID:
        return "TIMER_CONTRACT_INVALID";
    case CF09_MAX14830_GUARD_TIMER_FAILURE:
        return "TIMER_FAILURE";
    case CF09_MAX14830_GUARD_TIMER_ACCEPTANCE_UNKNOWN:
        return "TIMER_ACCEPTANCE_UNKNOWN";
    case CF09_MAX14830_GUARD_EARLY_EVENT:
        return "EARLY_EVENT";
    case CF09_MAX14830_GUARD_COUNTER_EXHAUSTED:
        return "COUNTER_EXHAUSTED";
    case CF09_MAX14830_GUARD_ARITHMETIC_OVERFLOW:
        return "ARITHMETIC_OVERFLOW";
    default:
        return "INVALID_ENUM";
    }
}
