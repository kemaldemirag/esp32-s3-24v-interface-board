#include "cf09_max14830_guard_scheduler_v1.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n",                          \
                    __FILE__, __LINE__, #condition);                        \
            return false;                                                   \
        }                                                                   \
    } while (0)

typedef struct {
    cf09_max14830_guard_scheduler_t *scheduler;
    cf09_max14830_guard_timer_snapshot_t
        timer[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    uint32_t configuration_generation[
        CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    bool read_timer_ok[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    bool read_configuration_ok[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    cf09_max14830_guard_timer_arm_result_t arm_result;
    bool cancel_ok[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    size_t arm_calls;
    size_t cancel_calls[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    cf09_max14830_guard_event_t last_armed;
    cf09_max14830_guard_event_t last_cancelled[
        CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    bool try_reentry_on_arm;
    cf09_max14830_guard_result_t reentry_result;
    bool try_consume_reentry_on_arm;
    cf09_a_attempt_token_t reentry_token;
    cf09_a_attempt_token_t reentry_released;
    cf09_max14830_guard_result_t reentry_consume_result;
} guard_fake_t;

static bool fake_read_timer(
    void *user,
    uint8_t uart,
    cf09_max14830_guard_timer_snapshot_t *snapshot_out)
{
    guard_fake_t *fake = (guard_fake_t *)user;

    if (fake == NULL || snapshot_out == NULL ||
        uart >= (uint8_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS ||
        !fake->read_timer_ok[uart]) {
        return false;
    }
    *snapshot_out = fake->timer[uart];
    return true;
}

static bool fake_read_configuration_generation(
    void *user,
    uint8_t uart,
    uint32_t *generation_out)
{
    guard_fake_t *fake = (guard_fake_t *)user;

    if (fake == NULL || generation_out == NULL ||
        uart >= (uint8_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS ||
        !fake->read_configuration_ok[uart]) {
        return false;
    }
    *generation_out = fake->configuration_generation[uart];
    return true;
}

static cf09_max14830_guard_timer_arm_result_t fake_arm_absolute(
    void *user,
    const cf09_max14830_guard_event_t *event)
{
    guard_fake_t *fake = (guard_fake_t *)user;

    if (fake == NULL || event == NULL) {
        return CF09_MAX14830_GUARD_TIMER_ARM_UNKNOWN_ACCEPTANCE;
    }
    fake->arm_calls++;
    fake->last_armed = *event;
    if (fake->try_reentry_on_arm && fake->scheduler != NULL) {
        fake->reentry_result = cf09_max14830_guard_scheduler_invalidate(
            fake->scheduler, event->uart);
    }
    if (fake->try_consume_reentry_on_arm && fake->scheduler != NULL) {
        fake->reentry_consume_result =
            cf09_max14830_guard_scheduler_consume_event(
                fake->scheduler,
                event,
                &fake->reentry_token,
                &fake->reentry_released);
    }
    return fake->arm_result;
}

static bool fake_cancel_absolute(
    void *user,
    const cf09_max14830_guard_event_t *event)
{
    guard_fake_t *fake = (guard_fake_t *)user;

    if (fake == NULL || event == NULL ||
        event->uart >=
            (uint8_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS) {
        return false;
    }
    fake->cancel_calls[event->uart]++;
    fake->last_cancelled[event->uart] = *event;
    return fake->cancel_ok[event->uart];
}

static void fake_defaults(guard_fake_t *fake)
{
    size_t index;

    memset(fake, 0, sizeof(*fake));
    fake->arm_result = CF09_MAX14830_GUARD_TIMER_ARM_ACCEPTED;
    for (index = 0U;
         index < (size_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS;
         ++index) {
        fake->timer[index].now_ticks = UINT64_C(1000) + index * 100U;
        fake->timer[index].timer_generation = (uint32_t)(9U + index);
        fake->timer[index].timer_hz_upper_bound = UINT64_C(1000000);
        fake->timer[index].minimum_reliable_delay_ticks = UINT64_C(1);
        fake->configuration_generation[index] =
            (uint32_t)(44U + index);
        fake->read_timer_ok[index] = true;
        fake->read_configuration_ok[index] = true;
        fake->cancel_ok[index] = true;
    }
}

static cf09_max14830_guard_platform_ops_t fake_ops(guard_fake_t *fake)
{
    cf09_max14830_guard_platform_ops_t ops;

    memset(&ops, 0, sizeof(ops));
    ops.user = fake;
    ops.read_timer = fake_read_timer;
    ops.read_configuration_generation =
        fake_read_configuration_generation;
    ops.arm_absolute = fake_arm_absolute;
    ops.cancel_absolute = fake_cancel_absolute;
    return ops;
}

static bool init_scheduler(
    cf09_max14830_guard_scheduler_t *scheduler,
    guard_fake_t *fake)
{
    cf09_max14830_guard_platform_ops_t ops;

    memset(scheduler, 0, sizeof(*scheduler));
    fake_defaults(fake);
    fake->scheduler = scheduler;
    ops = fake_ops(fake);
    return cf09_max14830_guard_scheduler_init(scheduler, &ops) ==
        CF09_MAX14830_GUARD_OK;
}

static cf09_max14830_timing_config_t timing_config_for_uart(uint8_t uart)
{
    cf09_max14830_timing_config_t config;

    memset(&config, 0, sizeof(config));
    config.profile =
        CF09_MAX14830_TIMING_PROFILE_NORMAL_ASYNC_UART;
    config.scope = CF09_MAX14830_TIMING_SCOPE_SERIALIZER_COMPLETE;
    config.logical_channel = (uint8_t)(uart + 2U);
    config.uart = uart;
    config.resource_generation = (uint32_t)(11U + uart);
    config.bus_generation = 22U;
    config.device_generation = 33U;
    config.configuration_generation = (uint32_t)(44U + uart);
    config.divisor_integer = 26U;
    config.brg_config = 0U;
    config.lcr = 3U; /* 8 data, no parity, one stop bit. */
    config.reference_hz_lower_bound = UINT64_C(48000000);
    config.reference_hz_upper_bound = UINT64_C(48000000);
    config.timer_hz_upper_bound = UINT64_C(1000000);
    return config;
}

static bool timing_plan_for_uart(
    uint8_t uart,
    cf09_max14830_timing_derived_t *derived_out)
{
    cf09_max14830_timing_config_t config =
        timing_config_for_uart(uart);

    return cf09_max14830_timing_derive_guard(&config, derived_out) ==
        CF09_MAX14830_TIMING_OK;
}

static cf09_a_attempt_token_t token_for_uart(uint8_t uart)
{
    cf09_a_attempt_token_t token;

    memset(&token, 0, sizeof(token));
    token.session.high = UINT64_C(0x0102030405060708);
    token.session.low = UINT64_C(0x1112131415161718);
    token.frame.epoch = 7U;
    token.frame.sequence = (uint32_t)(100U + uart);
    token.attempt_no = 1U;
    token.transfer_no = UINT64_C(500) + uart;
    token.logical_channel = (uint8_t)(uart + 2U);
    token.serializer_id = (uint8_t)(uart + 2U);
    token.resource_id = uart;
    token.physical_channel = uart;
    token.device_id = (uint8_t)CF09_A_DEVICE_ID;
    token.chip_select_id = (uint8_t)CF09_A_CHIP_SELECT_ID;
    token.resource_generation = (uint32_t)(11U + uart);
    token.bus_generation = 22U;
    token.device_generation = 33U;
    token.payload_len = 4U;
    token.payload_fingerprint = UINT64_C(0x8899aabbccddeeff);
    return token;
}

static bool admit_and_arm(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    cf09_max14830_timing_derived_t *derived_out,
    cf09_a_attempt_token_t *token_out,
    cf09_max14830_guard_event_t *event_out)
{
    CHECK(timing_plan_for_uart(uart, derived_out));
    *token_out = token_for_uart(uart);
    CHECK(cf09_max14830_guard_scheduler_admit(
              scheduler, derived_out, token_out) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              scheduler, uart, token_out, event_out) ==
          CF09_MAX14830_GUARD_OK);
    return true;
}

static bool test_init_validation_and_status(void)
{
    cf09_max14830_guard_scheduler_t scheduler =
        CF09_MAX14830_GUARD_SCHEDULER_INITIALIZER;
    cf09_max14830_guard_scheduler_t missing =
        CF09_MAX14830_GUARD_SCHEDULER_INITIALIZER;
    guard_fake_t fake;
    cf09_max14830_guard_platform_ops_t ops;
    cf09_max14830_guard_validation_t validation;

    fake_defaults(&fake);
    ops = fake_ops(&fake);
    CHECK(cf09_max14830_guard_scheduler_init(NULL, &ops) ==
          CF09_MAX14830_GUARD_INVALID_ARGUMENT);
    CHECK(cf09_max14830_guard_scheduler_init(&scheduler, NULL) ==
          CF09_MAX14830_GUARD_INVALID_ARGUMENT);
    ops.arm_absolute = NULL;
    CHECK(cf09_max14830_guard_scheduler_init(&missing, &ops) ==
          CF09_MAX14830_GUARD_INVALID_ARGUMENT);
    CHECK(!missing._initialized);

    ops = fake_ops(&fake);
    CHECK(cf09_max14830_guard_scheduler_init(&scheduler, &ops) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_init(&scheduler, &ops) ==
          CF09_MAX14830_GUARD_INVALID_STATE);
    validation = cf09_max14830_guard_scheduler_validate(&scheduler);
    CHECK(validation.initialized);
    CHECK(!validation.mutating);
    CHECK(validation.next_arm_generation == 1U);
    CHECK(validation.slot_state[0] ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    CHECK(strstr(validation.model_status, "NATIVE_TEST_PASS") != NULL);
    CHECK(strstr(validation.integration_status, "NOT_RUN") != NULL);
    return true;
}

static bool test_admit_deep_copy_busy_and_binding(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_derived_t changed;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t original;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    original = token;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_BUSY);

    changed = derived;
    derived.guard_ticks_minimum++;
    token.attempt_no++;
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_STALE_TOKEN);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &original, &event) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(event.alarm_value == fake.timer[0].now_ticks +
          changed.guard_ticks_minimum);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    /* Caller-derived arithmetic is never trusted; config is re-derived. */
    CHECK(timing_plan_for_uart(0U, &derived));
    changed = derived;
    derived.guard_ticks_minimum = 1U;
    token = token_for_uart(0U);
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(event.alarm_value == fake.timer[0].now_ticks +
          changed.guard_ticks_minimum);
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) == CF09_MAX14830_GUARD_OK);

    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    token.device_generation++;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) ==
          CF09_MAX14830_GUARD_BINDING_MISMATCH);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    derived.config_snapshot.divisor_integer = 0U;
    token = token_for_uart(0U);
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) ==
          CF09_MAX14830_GUARD_TIMING_PLAN_INVALID);
    return true;
}

static bool test_absolute_deadline_floor_and_no_inline_reentry(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_max14830_guard_event_t event;
    uint64_t floor;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    floor = derived.guard_ticks_minimum + UINT64_C(9);
    fake.timer[0].minimum_reliable_delay_ticks = floor;
    fake.try_reentry_on_arm = true;
    fake.try_consume_reentry_on_arm = true;
    fake.reentry_token = token;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(event.uart == 0U);
    CHECK(event.arm_generation == 1U);
    CHECK(event.alarm_value == fake.timer[0].now_ticks + floor);
    CHECK(fake.last_armed.alarm_value == event.alarm_value);
    CHECK(fake.reentry_result == CF09_MAX14830_GUARD_BUSY);
    CHECK(fake.reentry_consume_result == CF09_MAX14830_GUARD_BUSY);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    fake.timer[0].now_ticks = event.alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler,
              &event,
              &token,
              &fake.reentry_released) == CF09_MAX14830_GUARD_OK);
    return true;
}

static bool test_arm_revalidates_configuration_and_timer(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    fake.configuration_generation[0]++;
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_BINDING_MISMATCH);
    CHECK(fake.arm_calls == 0U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_FAULTED);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    fake.configuration_generation[0]--;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    fake.timer[0].timer_hz_upper_bound =
        derived.config_snapshot.timer_hz_upper_bound + 1U;
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID);
    CHECK(fake.arm_calls == 0U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_FAULTED);
    return true;
}

static bool test_deadline_and_generation_exhaustion(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    fake.timer[0].now_ticks =
        UINT64_MAX - derived.guard_ticks_minimum + 1U;
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_ARITHMETIC_OVERFLOW);
    CHECK(fake.arm_calls == 0U);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    fake.timer[0].now_ticks = 1000U;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    scheduler._next_arm_generation = UINT64_MAX;
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_COUNTER_EXHAUSTED);
    CHECK(fake.arm_calls == 0U);
    CHECK(scheduler._next_arm_generation == UINT64_MAX);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_FAULTED);
    return true;
}

static bool test_invalid_platform_evidence_fails_closed(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    fake.read_configuration_ok[0] = false;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) ==
          CF09_MAX14830_GUARD_TIMER_FAILURE);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);

    fake.read_configuration_ok[0] = true;
    fake.configuration_generation[0] = 0U;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) ==
          CF09_MAX14830_GUARD_TIMER_FAILURE);
    fake.configuration_generation[0] =
        derived.config_snapshot.configuration_generation;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    fake.read_timer_ok[0] = false;
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID);
    CHECK(fake.arm_calls == 0U);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    fake.read_timer_ok[0] = true;
    fake.timer[0].minimum_reliable_delay_ticks = 0U;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID);
    CHECK(fake.arm_calls == 0U);
    return true;
}

static bool test_arm_rejection_and_unknown_acceptance(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event;
    uint64_t previous_generation;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(timing_plan_for_uart(0U, &derived));
    token = token_for_uart(0U);
    fake.arm_result =
        CF09_MAX14830_GUARD_TIMER_ARM_REJECTED_NO_EFFECT;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_TIMER_FAILURE);
    CHECK(event.arm_generation == 0U);
    previous_generation = fake.last_armed.arm_generation;
    CHECK(previous_generation != 0U);
    CHECK(fake.cancel_calls[0] == 0U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_FAULTED);
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) == CF09_MAX14830_GUARD_OK);

    fake.arm_result =
        CF09_MAX14830_GUARD_TIMER_ARM_UNKNOWN_ACCEPTANCE;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_TIMER_ACCEPTANCE_UNKNOWN);
    CHECK(event.arm_generation == 0U);
    CHECK(fake.last_armed.arm_generation > previous_generation);
    previous_generation = fake.last_armed.arm_generation;
    CHECK(fake.cancel_calls[0] == 1U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_FAULTED);
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) == CF09_MAX14830_GUARD_OK);

    /* Unknown acceptance plus uncertain cancellation stays quarantined. */
    fake.cancel_ok[0] = false;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &event) ==
          CF09_MAX14830_GUARD_TIMER_ACCEPTANCE_UNKNOWN);
    CHECK(fake.last_armed.arm_generation > previous_generation);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_QUARANTINED);
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler,
              &fake.last_armed,
              &token,
              &released) == CF09_MAX14830_GUARD_STALE_EVENT);
    fake.cancel_ok[0] = true;
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 3U);
    return true;
}

static bool test_valid_event_is_exactly_once(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t original;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event;
    cf09_max14830_guard_validation_t validation;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    original = token;
    fake.timer[0].now_ticks = event.alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &token) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(cf09_a_token_equal(&token, &original));
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    memset(&released, 0xA5, sizeof(released));
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &original, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);
    CHECK(released.attempt_no == 0U);
    validation = cf09_max14830_guard_scheduler_validate(&scheduler);
    CHECK(validation.consumed_count == 1U);
    CHECK(validation.stale_event_count == 1U);
    return true;
}

static bool test_early_event_fails_closed(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.timer[0].now_ticks = event.alarm_value - 1U;
    fake.cancel_ok[0] = false;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_EARLY_EVENT);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_QUARANTINED);
    CHECK(released.attempt_no == 0U);
    fake.cancel_ok[0] = true;
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 2U);
    return true;
}

static bool test_worker_revalidates_token_config_and_timer(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t wrong;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.timer[0].now_ticks = event.alarm_value;
    wrong = token;
    wrong.transfer_no++;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &wrong, &released) ==
          CF09_MAX14830_GUARD_BINDING_MISMATCH);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.timer[0].now_ticks = event.alarm_value;
    fake.configuration_generation[0]++;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_BINDING_MISMATCH);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    fake.configuration_generation[0]--;
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.timer[0].now_ticks = event.alarm_value;
    fake.timer[0].timer_hz_upper_bound =
        derived.config_snapshot.timer_hz_upper_bound + 1U;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    fake.timer[0].timer_hz_upper_bound =
        derived.config_snapshot.timer_hz_upper_bound;
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.timer[0].now_ticks = event.alarm_value;
    fake.timer[0].minimum_reliable_delay_ticks =
        derived.guard_ticks_minimum + 1U;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);

    fake.timer[0].minimum_reliable_delay_ticks = 1U;
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.timer[0].now_ticks = event.alarm_value;
    fake.timer[0].timer_generation++;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID);
    return true;
}

static bool test_cancel_idempotence_and_queued_stale_event(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t wrong;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    wrong = token;
    wrong.attempt_no++;
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &wrong) ==
          CF09_MAX14830_GUARD_STALE_TOKEN);
    CHECK(fake.cancel_calls[0] == 0U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);

    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 1U);
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 1U);
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &wrong) ==
          CF09_MAX14830_GUARD_STALE_TOKEN);
    CHECK(fake.cancel_calls[0] == 1U);
    fake.timer[0].now_ticks = event.alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);
    return true;
}

static bool test_cancel_failure_quarantines_and_retries(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &event));
    fake.cancel_ok[0] = false;
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) ==
          CF09_MAX14830_GUARD_TIMER_FAILURE);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_QUARANTINED);
    fake.timer[0].now_ticks = event.alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event, &token, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);

    fake.cancel_ok[0] = true;
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 2U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    return true;
}

static bool test_event_identity_and_delivery_failure_seam(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived0;
    cf09_max14830_timing_derived_t derived1;
    cf09_a_attempt_token_t token0;
    cf09_a_attempt_token_t token1;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event0;
    cf09_max14830_guard_event_t event1;
    cf09_max14830_guard_event_t wrong;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived0, &token0, &event0));
    wrong = event0;
    wrong.alarm_value++;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &wrong, &token0, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    wrong = event0;
    wrong.arm_generation++;
    CHECK(cf09_max14830_guard_scheduler_report_delivery_failure(
              &scheduler, &wrong) == CF09_MAX14830_GUARD_STALE_EVENT);
    CHECK(fake.cancel_calls[0] == 0U);

    CHECK(admit_and_arm(
        &scheduler, 1U, &derived1, &token1, &event1));
    wrong = event0;
    wrong.uart = 1U;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &wrong, &token1, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 1U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);

    /* Queue-full/lost-wake report invalidates before cancellation. */
    fake.cancel_ok[0] = false;
    CHECK(cf09_max14830_guard_scheduler_report_delivery_failure(
              &scheduler, &event0) == CF09_MAX14830_GUARD_TIMER_FAILURE);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_QUARANTINED);
    fake.timer[0].now_ticks = event0.alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event0, &token0, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);
    fake.cancel_ok[0] = true;
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 0U) == CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 2U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 1U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    CHECK(cf09_max14830_guard_scheduler_invalidate(
              &scheduler, 1U) == CF09_MAX14830_GUARD_OK);
    return true;
}

static bool test_stale_old_event_does_not_disturb_new_alarm(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived;
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t old_token;
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t old_event;
    cf09_max14830_guard_event_t new_event;

    CHECK(init_scheduler(&scheduler, &fake));
    CHECK(admit_and_arm(
        &scheduler, 0U, &derived, &token, &old_event));
    old_token = token;
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &token) == CF09_MAX14830_GUARD_OK);

    token.attempt_no++;
    token.transfer_no++;
    token.frame.sequence++;
    fake.timer[0].now_ticks += 100U;
    CHECK(cf09_max14830_guard_scheduler_admit(
              &scheduler, &derived, &token) == CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_cancel(
              &scheduler, 0U, &old_token) ==
          CF09_MAX14830_GUARD_STALE_TOKEN);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ADMITTED);
    CHECK(cf09_max14830_guard_scheduler_arm(
              &scheduler, 0U, &token, &new_event) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(new_event.arm_generation != old_event.arm_generation);
    fake.timer[0].now_ticks = new_event.alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &old_event, &token, &released) ==
          CF09_MAX14830_GUARD_STALE_EVENT);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &new_event, &token, &released) ==
          CF09_MAX14830_GUARD_OK);
    return true;
}

static bool test_three_uart_slots_are_independent(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived[3];
    cf09_a_attempt_token_t token[3];
    cf09_a_attempt_token_t released;
    cf09_max14830_guard_event_t event[3];
    uint8_t uart;

    CHECK(init_scheduler(&scheduler, &fake));
    for (uart = 0U;
         uart < (uint8_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS;
         ++uart) {
        CHECK(admit_and_arm(
            &scheduler, uart, &derived[uart], &token[uart],
            &event[uart]));
    }
    CHECK(event[0].arm_generation != event[1].arm_generation);
    CHECK(event[1].arm_generation != event[2].arm_generation);
    fake.timer[1].now_ticks = event[1].alarm_value;
    CHECK(cf09_max14830_guard_scheduler_consume_event(
              &scheduler, &event[1], &token[1], &released) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 1U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 2U) ==
          CF09_MAX14830_GUARD_SLOT_ARMED);
    CHECK(cf09_max14830_guard_scheduler_invalidate_all(&scheduler) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[0] == 1U);
    CHECK(fake.cancel_calls[1] == 0U);
    CHECK(fake.cancel_calls[2] == 1U);
    return true;
}

static bool test_invalidate_all_attempts_every_slot(void)
{
    cf09_max14830_guard_scheduler_t scheduler;
    guard_fake_t fake;
    cf09_max14830_timing_derived_t derived[3];
    cf09_a_attempt_token_t token[3];
    cf09_max14830_guard_event_t event[3];
    uint8_t uart;

    CHECK(init_scheduler(&scheduler, &fake));
    for (uart = 0U;
         uart < (uint8_t)CF09_MAX14830_GUARD_SCHEDULER_UARTS;
         ++uart) {
        CHECK(admit_and_arm(
            &scheduler, uart, &derived[uart], &token[uart],
            &event[uart]));
    }
    fake.cancel_ok[1] = false;
    CHECK(cf09_max14830_guard_scheduler_invalidate_all(&scheduler) ==
          CF09_MAX14830_GUARD_TIMER_FAILURE);
    CHECK(fake.cancel_calls[0] == 1U);
    CHECK(fake.cancel_calls[1] == 1U);
    CHECK(fake.cancel_calls[2] == 1U);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 0U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 1U) ==
          CF09_MAX14830_GUARD_SLOT_QUARANTINED);
    CHECK(cf09_max14830_guard_scheduler_slot_state(&scheduler, 2U) ==
          CF09_MAX14830_GUARD_SLOT_IDLE);
    fake.cancel_ok[1] = true;
    CHECK(cf09_max14830_guard_scheduler_invalidate_all(&scheduler) ==
          CF09_MAX14830_GUARD_OK);
    CHECK(fake.cancel_calls[1] == 2U);
    return true;
}

static bool test_result_names(void)
{
    CHECK(strcmp(cf09_max14830_guard_result_name(
                     CF09_MAX14830_GUARD_OK),
                 "OK") == 0);
    CHECK(strcmp(cf09_max14830_guard_result_name(
                     CF09_MAX14830_GUARD_TIMER_ACCEPTANCE_UNKNOWN),
                 "TIMER_ACCEPTANCE_UNKNOWN") == 0);
    CHECK(strcmp(cf09_max14830_guard_result_name(
                     CF09_MAX14830_GUARD_TIMING_PLAN_INVALID),
                 "TIMING_PLAN_INVALID") == 0);
    CHECK(strcmp(cf09_max14830_guard_result_name(
                     (cf09_max14830_guard_result_t)999),
                 "INVALID_ENUM") == 0);
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"init_validation_and_status", test_init_validation_and_status},
        {"admit_deep_copy_busy_and_binding",
         test_admit_deep_copy_busy_and_binding},
        {"absolute_deadline_floor_and_no_inline_reentry",
         test_absolute_deadline_floor_and_no_inline_reentry},
        {"arm_revalidates_configuration_and_timer",
         test_arm_revalidates_configuration_and_timer},
        {"deadline_and_generation_exhaustion",
         test_deadline_and_generation_exhaustion},
        {"invalid_platform_evidence_fails_closed",
         test_invalid_platform_evidence_fails_closed},
        {"arm_rejection_and_unknown_acceptance",
         test_arm_rejection_and_unknown_acceptance},
        {"valid_event_is_exactly_once",
         test_valid_event_is_exactly_once},
        {"early_event_fails_closed", test_early_event_fails_closed},
        {"worker_revalidates_token_config_and_timer",
         test_worker_revalidates_token_config_and_timer},
        {"cancel_idempotence_and_queued_stale_event",
         test_cancel_idempotence_and_queued_stale_event},
        {"cancel_failure_quarantines_and_retries",
         test_cancel_failure_quarantines_and_retries},
        {"event_identity_and_delivery_failure_seam",
         test_event_identity_and_delivery_failure_seam},
        {"stale_old_event_does_not_disturb_new_alarm",
         test_stale_old_event_does_not_disturb_new_alarm},
        {"three_uart_slots_are_independent",
         test_three_uart_slots_are_independent},
        {"invalidate_all_attempts_every_slot",
         test_invalidate_all_attempts_every_slot},
        {"result_names", test_result_names}
    };
    size_t index;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "Test failed: %s\n", tests[index].name);
            return 1;
        }
        printf("PASS %s\n", tests[index].name);
    }
    printf("PASS %zu tests\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
