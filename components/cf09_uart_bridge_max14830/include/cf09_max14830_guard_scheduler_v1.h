#ifndef CF09_MAX14830_GUARD_SCHEDULER_V1_H
#define CF09_MAX14830_GUARD_SCHEDULER_V1_H

/*
 * Platform-neutral, fail-closed one-shot scheduler for the MAX14830 final
 * character guard.
 *
 * Evidence and integration boundary:
 *   - The guard length comes only from cf09_max14830_timing_contract_v1.
 *     No clock, baud, timer rate or minimum-delay value is invented here.
 *   - A caller-qualified timer snapshot supplies a monotonic counter, a
 *     conservative upper bound on its tick rate and a qualified minimum
 *     reliable one-shot delay.  The scheduled delay is the greater of that
 *     minimum and the derived character guard.
 *   - The timer adapter receives an immutable {UART, generation, deadline}
 *     descriptor.  Its ISR may copy that descriptor to an ISR-safe queue,
 *     but must not access this scheduler or call the backend/core.
 *   - consume_event() is worker-context only.  It revalidates the full token,
 *     configuration generation, timer-rate bound, alarm generation, deadline
 *     and current counter before releasing a token exactly once.
 *   - Cancellation invalidates the slot before touching timer hardware.
 *     Thus an already queued event is stale.  A failed cancellation leaves a
 *     quarantined slot that must be retried before the slot can be reused.
 *   - One scheduler owns three independent UART slots.  The owner task must
 *     serialize all calls.  Volatile is deliberately not used as a locking
 *     substitute on ESP32-S3 multicore.
 *
 * ASSUMPTION / required future integration:
 *   A future backend admission hook must call admit() after the complete token
 *   is created and before SPI submission starts serialization.  The current
 *   backend_v1 arm callback occurs only after ISR[5] and is too late to prove
 *   configuration immutability across the whole transmission.  Until that
 *   hook and the ESP-IDF adapter exist, this component is not production
 *   integrated.
 */

#include "cf09_max14830_timing_contract_v1.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF09_MAX14830_GUARD_SCHEDULER_INITIALIZER {0}
#define CF09_MAX14830_GUARD_SCHEDULER_MODEL_STATUS \
    "MODEL_ONLY / NATIVE_TEST_PASS / INTEGRATION_NOT_RUN"
#define CF09_MAX14830_GUARD_SCHEDULER_INTEGRATION_STATUS \
    "NOT_RUN / BACKEND_ADMISSION_HOOK_AND_ESP_IDF_ADAPTER_REQUIRED"
#define CF09_MAX14830_GUARD_SCHEDULER_ISR_POLICY \
    "COPY_EVENT_ONLY / NO_SCHEDULER_OR_BACKEND_CALL_FROM_ISR"

enum {
    CF09_MAX14830_GUARD_SCHEDULER_UARTS = 3
};

typedef enum {
    CF09_MAX14830_GUARD_OK = 0,
    CF09_MAX14830_GUARD_INVALID_ARGUMENT,
    CF09_MAX14830_GUARD_INVALID_STATE,
    CF09_MAX14830_GUARD_BUSY,
    CF09_MAX14830_GUARD_STALE_TOKEN,
    CF09_MAX14830_GUARD_STALE_EVENT,
    CF09_MAX14830_GUARD_BINDING_MISMATCH,
    CF09_MAX14830_GUARD_TIMING_PLAN_INVALID,
    CF09_MAX14830_GUARD_TIMER_CONTRACT_INVALID,
    CF09_MAX14830_GUARD_TIMER_FAILURE,
    CF09_MAX14830_GUARD_TIMER_ACCEPTANCE_UNKNOWN,
    CF09_MAX14830_GUARD_EARLY_EVENT,
    CF09_MAX14830_GUARD_COUNTER_EXHAUSTED,
    CF09_MAX14830_GUARD_ARITHMETIC_OVERFLOW
} cf09_max14830_guard_result_t;

typedef enum {
    CF09_MAX14830_GUARD_SLOT_IDLE = 0,
    CF09_MAX14830_GUARD_SLOT_ADMITTED,
    CF09_MAX14830_GUARD_SLOT_ARMED,
    /* Active identity is invalid; timer cancellation must be retried. */
    CF09_MAX14830_GUARD_SLOT_QUARANTINED,
    /* No completion can be released; reset/invalidate is required. */
    CF09_MAX14830_GUARD_SLOT_FAULTED
} cf09_max14830_guard_slot_state_t;

typedef enum {
    /* Absolute alarm accepted; the descriptor may later be queued. */
    CF09_MAX14830_GUARD_TIMER_ARM_ACCEPTED = 0,
    /* Rejected with a guarantee that no event was retained. */
    CF09_MAX14830_GUARD_TIMER_ARM_REJECTED_NO_EFFECT,
    /* Acceptance cannot be proven; cancellation/quarantine is mandatory. */
    CF09_MAX14830_GUARD_TIMER_ARM_UNKNOWN_ACCEPTANCE
} cf09_max14830_guard_timer_arm_result_t;

typedef struct {
    uint8_t uart;
    uint64_t arm_generation;
    uint64_t alarm_value;
} cf09_max14830_guard_event_t;

typedef struct {
    uint64_t now_ticks;
    /* Must advance before counter reset or timer-source/reliability change. */
    uint32_t timer_generation;
    /* Conservative current upper bound on actual counter ticks per second. */
    uint64_t timer_hz_upper_bound;
    /* Source-qualified platform floor; zero is not accepted. */
    uint64_t minimum_reliable_delay_ticks;
} cf09_max14830_guard_timer_snapshot_t;

typedef bool (*cf09_max14830_guard_read_timer_fn)(
    void *user,
    uint8_t uart,
    cf09_max14830_guard_timer_snapshot_t *snapshot_out);

typedef bool (*cf09_max14830_guard_read_configuration_generation_fn)(
    void *user,
    uint8_t uart,
    uint32_t *configuration_generation_out);

/*
 * event points to call-scoped storage and must be deep-copied synchronously.
 * ACCEPTED means the exact alarm_value was still in the future when committed
 * and was programmed as a one-shot.  A port must not relabel an old callback
 * from mutable current-event storage; the per-arm identity remains immutable
 * until callback/cancel drain.
 */
typedef cf09_max14830_guard_timer_arm_result_t
    (*cf09_max14830_guard_arm_absolute_fn)(
        void *user,
        const cf09_max14830_guard_event_t *event);

/*
 * True guarantees that this alarm is inactive.  An event already copied to a
 * queue may still arrive, but will be stale because the slot was invalidated
 * before this function was called.  Repeating the same cancellation must be
 * safe.  event points to call-scoped storage; the port must copy anything it
 * needs after this function returns.
 */
typedef bool (*cf09_max14830_guard_cancel_absolute_fn)(
    void *user,
    const cf09_max14830_guard_event_t *event);

typedef struct {
    void *user;
    cf09_max14830_guard_read_timer_fn read_timer;
    cf09_max14830_guard_read_configuration_generation_fn
        read_configuration_generation;
    cf09_max14830_guard_arm_absolute_fn arm_absolute;
    cf09_max14830_guard_cancel_absolute_fn cancel_absolute;
} cf09_max14830_guard_platform_ops_t;

typedef struct {
    cf09_max14830_guard_slot_state_t state;
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_binding_t binding;
    cf09_max14830_guard_event_t alarm;
    uint32_t timer_generation;
    uint64_t scheduled_delay_ticks;
    bool cancellation_tombstone_valid;
    cf09_a_attempt_token_t cancellation_tombstone;
} cf09_max14830_guard_slot_t;

typedef struct {
    bool _initialized;
    bool _mutating;
    uint64_t _next_arm_generation;
    cf09_max14830_guard_platform_ops_t _platform;
    cf09_max14830_guard_slot_t
        _slots[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    uint64_t _admitted_count;
    uint64_t _armed_count;
    uint64_t _consumed_count;
    uint64_t _stale_event_count;
    uint64_t _fault_count;
} cf09_max14830_guard_scheduler_t;

typedef struct {
    bool initialized;
    bool mutating;
    uint64_t next_arm_generation;
    cf09_max14830_guard_slot_state_t
        slot_state[CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    bool cancellation_tombstone_valid[
        CF09_MAX14830_GUARD_SCHEDULER_UARTS];
    uint64_t admitted_count;
    uint64_t armed_count;
    uint64_t consumed_count;
    uint64_t stale_event_count;
    uint64_t fault_count;
    const char *model_status;
    const char *integration_status;
    const char *isr_policy;
} cf09_max14830_guard_validation_t;

/*
 * scheduler storage must be all-zero or use
 * CF09_MAX14830_GUARD_SCHEDULER_INITIALIZER before the first init.  The timer
 * port must provide one independent alarm per UART or a proven multi-deadline
 * arbiter that preserves all three absolute deadlines.
 */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_init(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_guard_platform_ops_t *platform);

/*
 * Worker/owner context only.  Must run at TX admission before serialization.
 * Re-derives a canonical plan from derived->config_snapshot (all other caller
 * derived fields are ignored), then binds the complete immutable attempt
 * token.  This prevents a shortened caller-supplied guard from being trusted.
 */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_admit(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_timing_derived_t *derived,
    const cf09_a_attempt_token_t *token);

/*
 * Called by the backend platform arm wrapper after a fresh ISR[5].  It reads
 * current config/timer evidence, checked-adds an absolute deadline, allocates
 * a non-reusable generation and arms a one-shot alarm.  The platform must not
 * call this scheduler or the backend inline from arm_absolute().
 */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_arm(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    const cf09_a_attempt_token_t *token,
    cf09_max14830_guard_event_t *event_out);

/*
 * Worker context only.  On success the slot becomes IDLE before the token is
 * returned, so duplicate events are stale.  current_token must be a fresh
 * live-backend snapshot, not a copy retained only by this scheduler.  The
 * caller owns the returned token/action and may then call
 * backend_on_guard_elapsed outside this object.  It must retain and retry that
 * action if the backend reports a retryable result (for example REENTRANT),
 * and must not admit a new attempt on that UART until the backend accepts the
 * action or the integration fails closed.
 */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_consume_event(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_guard_event_t *event,
    const cf09_a_attempt_token_t *current_token,
    cf09_a_attempt_token_t *token_out);

/*
 * Worker-context fault seam for an ISR adapter that could not enqueue an
 * otherwise exact alarm event.  The ISR must first latch/copy the descriptor
 * and completion-path-unhealthy evidence using platform synchronization, and
 * guarantee an owner-task wake or poll.  This function then invalidates and
 * cancels the alarm.  It never releases completion.
 */
cf09_max14830_guard_result_t
cf09_max14830_guard_scheduler_report_delivery_failure(
    cf09_max14830_guard_scheduler_t *scheduler,
    const cf09_max14830_guard_event_t *event);

/* Exact-token, idempotent cancellation for a backend platform wrapper. */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_cancel(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart,
    const cf09_a_attempt_token_t *token);

/* Reset/quiesce path.  Invalidates first, then cancels or retries cancellation. */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_invalidate(
    cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart);

/* Attempts every UART even if one cancellation fails. */
cf09_max14830_guard_result_t cf09_max14830_guard_scheduler_invalidate_all(
    cf09_max14830_guard_scheduler_t *scheduler);

cf09_max14830_guard_slot_state_t cf09_max14830_guard_scheduler_slot_state(
    const cf09_max14830_guard_scheduler_t *scheduler,
    uint8_t uart);

cf09_max14830_guard_validation_t cf09_max14830_guard_scheduler_validate(
    const cf09_max14830_guard_scheduler_t *scheduler);

const char *cf09_max14830_guard_result_name(
    cf09_max14830_guard_result_t result);

#ifdef __cplusplus
}
#endif

#endif
