#ifndef CF09_MAX14830_BACKEND_V1_H
#define CF09_MAX14830_BACKEND_V1_H

/*
 * Stateful, platform-neutral MAX14830 candidate backend for the CF-09
 * class-A / MIRRORED_5 portable core.
 *
 * Evidence boundary:
 *   MODEL_ONLY / HOST_NATIVE_TEST_PASS / ISR_PATCH_NATIVE_PASS
 *   exact orderable part and electrical design: UNKNOWN
 *   ESP-IDF, PCB, connector and bench validation: NOT_RUN
 *
 * The backend deliberately contains no baud, LCR, clock, GPIO, timeout or
 * rail value not present in the selected evidence.  Channel configuration is
 * therefore an explicit caller attestation bound to one resource, bus and
 * device generation.  It is not a register-programming PASS.
 *
 * The platform output gate must be independent of a functioning SPI bus.
 * control_transfer is synchronous and bounded by the platform.  start_transfer
 * is asynchronous and must never call the core inline.  ISR[5] is only the
 * start of the final character: a platform-provided full-character guard is
 * required before a completion event can be released to the core.  Guard
 * callbacks must be deferred; they must not re-enter the backend inline from
 * arm_final_character_guard.  abort_transfer and reset cleanup hooks must be
 * idempotent so a partially failed reset catch-up can be retried safely.
 */

#include "cf09_max14830_spi_codec_v2.h"
#include "cf09_uart_bridge_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF09_MAX14830_BACKEND_INITIALIZER {0}
#define CF09_MAX14830_BACKEND_MODEL_STATUS \
    "MODEL_ONLY / HOST_NATIVE_TEST_PASS / ISR_PATCH_NATIVE_PASS"
#define CF09_MAX14830_BACKEND_HARDWARE_STATUS \
    "NOT_RUN / ESP_IDF_AND_HARDWARE_REQUIRED"
#define CF09_MAX14830_BACKEND_CONFIG_STATUS \
    "CALLER_ATTESTED / EXACT_VALUES_UNKNOWN"
#define CF09_MAX14830_BACKEND_UART3_IRQ_POLICY \
    "BOOT_STATUS_CLEARED / RUNTIME_IRQ_MUST_REMAIN_MASKED_UNTIL_RX_EXISTS"

enum {
    CF09_MAX14830_BACKEND_TX_CHANNELS = 3,
    CF09_MAX14830_BACKEND_ALL_CHANNELS_MASK = 7,
    /* Rev. 7 ISR[5]: final TxFIFO word started transmitting. */
    CF09_MAX14830_BACKEND_ISR_TX_EMPTY_BIT =
        CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT,
    /* ISR[6] (0x40) is RX-FIFO-empty and is never TX-path evidence. */
    CF09_MAX14830_BACKEND_ISR_ALLOWED_TX_MASK =
        CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT
};

typedef enum {
    CF09_MAX14830_BACKEND_OK = 0,
    CF09_MAX14830_BACKEND_INVALID_ARGUMENT,
    CF09_MAX14830_BACKEND_INVALID_STATE,
    CF09_MAX14830_BACKEND_NOT_READY,
    CF09_MAX14830_BACKEND_BUSY,
    CF09_MAX14830_BACKEND_STALE_TOKEN,
    CF09_MAX14830_BACKEND_UNSUPPORTED_SILICON,
    CF09_MAX14830_BACKEND_IO_FAILURE,
    CF09_MAX14830_BACKEND_IO_UNKNOWN,
    CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED
} cf09_max14830_backend_result_t;

typedef enum {
    CF09_MAX14830_BACKEND_IO_OK = 0,
    CF09_MAX14830_BACKEND_IO_BUSY,
    CF09_MAX14830_BACKEND_IO_REJECTED,
    CF09_MAX14830_BACKEND_IO_NOT_COMMITTED,
    CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE,
    CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE
} cf09_max14830_backend_io_result_t;

typedef enum {
    CF09_MAX14830_BACKEND_HOST_EMPTY = 0,
    CF09_MAX14830_BACKEND_HOST_IN_FLIGHT,
    CF09_MAX14830_BACKEND_HOST_RESULT_READY,
    CF09_MAX14830_BACKEND_HOST_UNCERTAIN
} cf09_max14830_backend_host_state_t;

typedef enum {
    CF09_MAX14830_BACKEND_CHANNEL_EMPTY = 0,
    CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING,
    CF09_MAX14830_BACKEND_CHANNEL_TAIL_PENDING,
    CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY,
    CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN
} cf09_max14830_backend_channel_state_t;

typedef cf09_max14830_backend_io_result_t
    (*cf09_max14830_backend_control_transfer_fn)(
        void *user,
        const uint8_t *tx_bytes,
        size_t tx_len,
        uint8_t *rx_bytes,
        size_t rx_capacity,
        size_t *rx_len_out);

typedef cf09_max14830_backend_io_result_t
    (*cf09_max14830_backend_start_transfer_fn)(
        void *user,
        const uint8_t *tx_bytes,
        size_t tx_len);

typedef bool (*cf09_max14830_backend_abort_transfer_fn)(void *user);
typedef bool (*cf09_max14830_backend_set_reset_fn)(
    void *user,
    bool asserted);
typedef bool (*cf09_max14830_backend_wait_ready_fn)(void *user);
typedef bool (*cf09_max14830_backend_set_output_gate_fn)(
    void *user,
    bool enabled);
typedef bool (*cf09_max14830_backend_guard_fn)(
    void *user,
    uint8_t uart,
    const cf09_a_attempt_token_t *token);
typedef bool (*cf09_max14830_backend_quiesce_fn)(void *user);
typedef cf09_a_completion_path_health_t
    (*cf09_max14830_backend_completion_health_fn)(void *user);

typedef struct {
    void *user;
    cf09_max14830_backend_control_transfer_fn control_transfer;
    cf09_max14830_backend_start_transfer_fn start_transfer;
    cf09_max14830_backend_abort_transfer_fn abort_transfer;
    cf09_max14830_backend_set_reset_fn set_reset;
    cf09_max14830_backend_wait_ready_fn wait_ready;
    cf09_max14830_backend_set_output_gate_fn set_output_gate;
    cf09_max14830_backend_guard_fn arm_final_character_guard;
    cf09_max14830_backend_guard_fn cancel_final_character_guard;
    cf09_max14830_backend_quiesce_fn quiesce;
    cf09_max14830_backend_completion_health_fn completion_health;
} cf09_max14830_backend_platform_ops_t;

typedef struct {
    cf09_max14830_backend_host_state_t state;
    cf09_a_attempt_token_t token;
    cf09_max14830_spi_frame_t frame;
    uint8_t payload[CF09_MAX14830_FIFO_CAPACITY_WORDS];
    size_t payload_len;
    cf09_a_commit_result_t result;
} cf09_max14830_backend_host_slot_t;

typedef struct {
    cf09_max14830_backend_channel_state_t state;
    cf09_a_attempt_token_t token;
    cf09_a_completion_source_t tail_source;
    uint8_t last_isr;
    bool guard_armed;
} cf09_max14830_backend_channel_slot_t;

typedef struct {
    bool _initialized;
    bool _mutating;
    bool _qualified_b4;
    bool _outputs_enabled;
    bool _reset_active;
    bool _faulted;
    bool _extended_state_unknown;
    bool _quiesced;
    bool _ever_probed;
    bool _uart3_irq_masked;
    bool _reset_catchup_pending;
    bool _reset_catchup_bus;
    uint8_t _revision_value;
    cf09_max14830_revision_class_t _revision_class;
    uint8_t _configured_mask;
    uint8_t _idle_mask;
    uint32_t _bus_generation;
    uint32_t _device_generation;
    uint32_t _attested_bus_generation;
    uint32_t _attested_device_generation;
    uint32_t _last_probe_device_generation;
    uint32_t _resource_generation[CF09_MAX14830_BACKEND_TX_CHANNELS];
    cf09_max14830_backend_platform_ops_t _platform;
    cf09_max14830_backend_host_slot_t _host;
    cf09_max14830_backend_channel_slot_t
        _channels[CF09_MAX14830_BACKEND_TX_CHANNELS];
    uint64_t _started_count;
    uint64_t _tail_count;
    uint64_t _completion_count;
    uint64_t _rejected_event_count;
} cf09_max14830_backend_t;

typedef struct {
    bool initialized;
    bool qualified_b4;
    bool outputs_enabled;
    bool reset_active;
    bool faulted;
    bool extended_state_unknown;
    bool quiesced;
    bool uart3_irq_masked;
    bool reset_catchup_pending;
    uint8_t revision_value;
    cf09_max14830_revision_class_t revision_class;
    uint8_t configured_mask;
    uint8_t idle_mask;
    uint32_t bus_generation;
    uint32_t device_generation;
    cf09_max14830_backend_host_state_t host_state;
    size_t active_channels;
    size_t uncertain_channels;
    uint64_t started_count;
    uint64_t tail_count;
    uint64_t completion_count;
    uint64_t rejected_event_count;
    cf09_a_completion_path_health_t completion_health;
    const char *model_status;
    const char *hardware_status;
    const char *configuration_status;
    const char *uart3_irq_policy;
} cf09_max14830_backend_validation_t;

cf09_max14830_backend_result_t cf09_max14830_backend_init(
    cf09_max14830_backend_t *backend,
    const cf09_max14830_backend_platform_ops_t *platform);

/*
 * Reset, bounded readiness, CE -> RevID -> CD, all-UART ISR clear and
 * empty-TX check.  After the first successful probe, a matching generic-core
 * reset through dispatch_reset is required before probing again.
 */
cf09_max14830_backend_result_t cf09_max14830_backend_reset_and_probe(
    cf09_max14830_backend_t *backend);

/* Caller attestation only; no baud/clock/register value is invented here. */
cf09_max14830_backend_result_t
cf09_max14830_backend_attest_channel_configured(
    cf09_max14830_backend_t *backend,
    uint8_t logical_channel,
    uint32_t resource_generation);

/* Caller attests that unimplemented UART3 cannot assert the shared IRQ. */
cf09_max14830_backend_result_t
cf09_max14830_backend_attest_uart3_irq_masked(
    cf09_max14830_backend_t *backend,
    uint32_t device_generation);

/* Reads TxFIFOLvl and records same-generation idle evidence. */
cf09_max14830_backend_result_t cf09_max14830_backend_verify_channel_idle(
    cf09_max14830_backend_t *backend,
    uint8_t logical_channel,
    uint32_t resource_generation);

/*
 * The only production host-result entry point.  REENTRANT means the caller
 * must retain this one-shot result and retry after the core leaves mutation.
 */
cf09_a_result_t cf09_max14830_backend_dispatch_host_result(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result);

/* Reads one UART ISR; ISR[5] arms a guard but is not completion. */
cf09_max14830_backend_result_t cf09_max14830_backend_observe_tail(
    cf09_max14830_backend_t *backend,
    uint8_t logical_channel,
    cf09_a_completion_source_t source,
    cf09_a_attempt_token_t *token_out);

/* Platform calls this only after its externally derived full-character guard. */
cf09_max14830_backend_result_t cf09_max14830_backend_on_guard_elapsed(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t *source_out);

/* The only production completion entry point. */
cf09_a_result_t cf09_max14830_backend_dispatch_completion(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t source);

#ifdef CF09_MAX14830_BACKEND_TESTING
/* Native-test hooks; excluded from the production API surface. */
cf09_max14830_backend_result_t
cf09_max14830_backend_test_record_host_result(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result);

cf09_max14830_backend_result_t
cf09_max14830_backend_test_release_completion(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token);

cf09_max14830_backend_result_t
cf09_max14830_backend_test_notify_reset(
    cf09_max14830_backend_t *backend,
    bool bus_reset);
#endif

cf09_max14830_backend_result_t cf09_max14830_backend_mark_timeout(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token);

/* Enforces core-reset first, then the matching backend generation advance. */
cf09_a_result_t cf09_max14830_backend_dispatch_reset(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver,
    bool bus_reset);

/* Retries only the backend half after dispatch_reset advanced the core. */
cf09_a_result_t cf09_max14830_backend_retry_reset_catchup(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver);

cf09_a_backend_ops_t cf09_max14830_backend_core_ops(
    cf09_max14830_backend_t *backend);

cf09_max14830_backend_result_t cf09_max14830_backend_get_generations(
    const cf09_max14830_backend_t *backend,
    uint32_t *bus_generation_out,
    uint32_t *device_generation_out);

cf09_max14830_backend_channel_state_t
cf09_max14830_backend_channel_state(
    const cf09_max14830_backend_t *backend,
    uint8_t logical_channel);

cf09_max14830_backend_validation_t cf09_max14830_backend_validate(
    const cf09_max14830_backend_t *backend);

const char *cf09_max14830_backend_result_name(
    cf09_max14830_backend_result_t result);

#ifdef __cplusplus
}
#endif

#endif
