#ifndef CF09_UART_BRIDGE_V1_H
#define CF09_UART_BRIDGE_V1_H

/*
 * CF-09 class-A / MIRRORED_5 portable transport seam.
 *
 * Evidence boundary:
 *   ASSUMPTION / MODEL_ONLY
 *   exact UART bridge: UNKNOWN / NOT_SELECTED
 *   hardware and electrical validation: NOT_RUN / HARDWARE_REQUIRED
 *   connector delivery: NOT_RUN / NOT_PROVEN
 *
 * This API deliberately contains no ESP-IDF, FreeRTOS, SPI, GPIO, register,
 * FIFO, baud-rate or timeout assumptions.  The backend owns those details.
 * All calls, including queries, require one owner task or the same external
 * critical section; the internal guard only rejects same-thread re-entry and
 * is not a lock.
 * The caller supplies a non-zero session identity that must not be reused for
 * the maximum possible late-callback lifetime.  The backend and its user
 * storage must outlive this driver instance.
 *
 * The backend must copy both token value and payload bytes into backend-owned
 * storage before returning STARTED or UNKNOWN_ACCEPTANCE; it must never retain
 * either caller pointer.  BUSY, REJECTED and FAILURE retain nothing.
 * Frame identities must increase lexicographically
 * by (epoch, sequence) within each logical channel; retries keep one frame ID.
 * payload_fingerprint is a deterministic accidental-corruption check, not a
 * cryptographic authenticator or a substitute for trusted callback routing.
 * Tokens are process-local callback values: never persist, transmit or treat
 * the padded/raw C struct as a wire format.  Channel 3 is reserved only; this
 * version does not implement or validate the RX data path.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF09_A_MODEL_CLASS "ASSUMPTION / MODEL_ONLY"
#define CF09_A_HARDWARE_STATUS "NOT_RUN / HARDWARE_REQUIRED"
#define CF09_A_EXACT_PART_STATUS "UNKNOWN / NOT_SELECTED"
#define CF09_A_CONNECTOR_STATUS "NOT_RUN / NOT_PROVEN"
#define CF09_A_RX_STATUS "RESERVATION_ONLY / RX_PATH_NOT_IMPLEMENTED / NOT_RUN"
#define CF09_A_COMPLETION_SCOPE \
    "MODEL_SERIALIZER_COMPLETE / NOT_CONNECTOR_DELIVERY"
#define CF09_A_DRIVER_INITIALIZER {0}

enum {
    CF09_A_EXTERNAL_LOGICAL_FIRST = 2,
    CF09_A_EXTERNAL_LOGICAL_LAST = 4,
    CF09_A_EXTERNAL_TX_COUNT = 3,
    CF09_A_EXTERNAL_TX_COMPLETE_MASK = 7,
    CF09_A_RX_RESERVED_PHYSICAL_CHANNEL = 3,
    CF09_A_DEVICE_ID = 1,
    CF09_A_CHIP_SELECT_ID = 1
};

typedef enum {
    CF09_A_RESULT_OK = 0,
    CF09_A_RESULT_INVALID_ARGUMENT,
    CF09_A_RESULT_INVALID_STATE,
    CF09_A_RESULT_NOT_READY,
    CF09_A_RESULT_BUSY,
    CF09_A_RESULT_BACKEND_REJECTED,
    CF09_A_RESULT_BACKEND_FAILURE,
    CF09_A_RESULT_STALE_TOKEN,
    CF09_A_RESULT_NOT_COMMITTED,
    CF09_A_RESULT_UNCERTAIN,
    CF09_A_RESULT_COUNTER_EXHAUSTED,
    CF09_A_RESULT_REENTRANT
} cf09_a_result_t;

typedef enum {
    CF09_A_BACKEND_STARTED = 0,
    CF09_A_BACKEND_BUSY,
    CF09_A_BACKEND_REJECTED,
    CF09_A_BACKEND_FAILURE,
    CF09_A_BACKEND_UNKNOWN_ACCEPTANCE
} cf09_a_backend_start_result_t;

typedef enum {
    CF09_A_COMMIT_COMMITTED = 0,
    CF09_A_COMMIT_NOT_COMMITTED,
    CF09_A_COMMIT_UNKNOWN
} cf09_a_commit_result_t;

typedef enum {
    CF09_A_COMPLETION_IRQ = 0,
    CF09_A_COMPLETION_STATUS_POLL
} cf09_a_completion_source_t;

typedef enum {
    CF09_A_COMPLETION_PATH_HEALTHY = 0,
    CF09_A_COMPLETION_PATH_IRQ_ASSERTED_WITHOUT_LATCH,
    CF09_A_COMPLETION_PATH_IRQ_STUCK,
    CF09_A_COMPLETION_PATH_UNKNOWN
} cf09_a_completion_path_health_t;

typedef enum {
    CF09_A_ATTEMPT_EMPTY = 0,
    CF09_A_ATTEMPT_HOST_PENDING,
    CF09_A_ATTEMPT_NOT_COMMITTED,
    CF09_A_ATTEMPT_HOST_COMMITTED,
    CF09_A_ATTEMPT_SERIALIZER_DONE_LATCHED,
    CF09_A_ATTEMPT_COMPLETE,
    CF09_A_ATTEMPT_UNCERTAIN
} cf09_a_attempt_state_t;

typedef enum {
    CF09_A_MODEL_NOT_RUN = 0,
    CF09_A_MODEL_NOT_READY,
    CF09_A_MODEL_PASS,
    CF09_A_MODEL_UNCERTAIN,
    CF09_A_MODEL_ERROR
} cf09_a_model_status_t;

typedef struct {
    uint64_t high;
    uint64_t low;
} cf09_a_session_id_t;

typedef struct {
    uint32_t epoch;
    uint32_t sequence;
} cf09_a_frame_id_t;

typedef struct {
    cf09_a_session_id_t session;
    cf09_a_frame_id_t frame;
    uint32_t attempt_no;
    uint64_t transfer_no;
    uint8_t logical_channel;
    uint8_t serializer_id;
    uint8_t resource_id;
    uint8_t physical_channel;
    uint8_t device_id;
    uint8_t chip_select_id;
    uint32_t resource_generation;
    uint32_t bus_generation;
    uint32_t device_generation;
    size_t payload_len;
    uint64_t payload_fingerprint;
} cf09_a_attempt_token_t;

typedef struct {
    uint8_t logical_channel;
    uint8_t serializer_id;
    uint8_t resource_id;
    uint8_t physical_channel;
    const char *ttl_endpoint;
    const char *rs232_endpoint;
} cf09_a_route_t;

typedef struct {
    cf09_a_frame_id_t frame;
    uint8_t logical_channel;
    const uint8_t *payload;
    size_t payload_len;
} cf09_a_tx_request_t;

typedef cf09_a_backend_start_result_t (*cf09_a_backend_start_tx_fn)(
    void *user,
    const cf09_a_attempt_token_t *token,
    const uint8_t *payload,
    size_t payload_len);

/*
 * BUSY, REJECTED and FAILURE must have no transfer side effect or retained
 * payload.  UNKNOWN_ACCEPTANCE means a copied transfer may be active and
 * requires abort; core returns its immutable token as UNCERTAIN evidence.
 * No backend may call a core callback inline from start_tx.
 */

typedef bool (*cf09_a_backend_close_host_tx_fn)(
    void *user,
    const cf09_a_attempt_token_t *token);

typedef bool (*cf09_a_backend_abort_host_tx_fn)(
    void *user,
    const cf09_a_attempt_token_t *token);

typedef bool (*cf09_a_backend_set_outputs_enabled_fn)(
    void *user,
    bool enabled);

/* Must detach event sources and guarantee that no callback can arrive later. */
typedef bool (*cf09_a_backend_quiesce_fn)(void *user);

/* Read-only health snapshot; it must not mutate backend state. */
typedef cf09_a_completion_path_health_t
    (*cf09_a_backend_completion_health_fn)(void *user);

typedef struct {
    void *user;
    cf09_a_backend_start_tx_fn start_tx;
    cf09_a_backend_close_host_tx_fn close_host_tx;
    cf09_a_backend_abort_host_tx_fn abort_host_tx;
    cf09_a_backend_set_outputs_enabled_fn set_outputs_enabled;
    cf09_a_backend_quiesce_fn quiesce;
    cf09_a_backend_completion_health_fn completion_health;
} cf09_a_backend_ops_t;

typedef struct {
    bool occupied;
    cf09_a_attempt_state_t state;
    cf09_a_attempt_token_t token;
} cf09_a_attempt_slot_t;

/*
 * Concrete storage avoids heap allocation in the portable core.  Members
 * prefixed with '_' are implementation-private and must not be changed by a
 * caller.  Use CF09_A_DRIVER_INITIALIZER (or equivalent all-zero storage)
 * before the first init and use the query functions for inspection.
 */
typedef struct {
    bool _initialized;
    bool _mutating;
    bool _bridge_up;
    bool _reset_active;
    bool _outputs_armed;
    bool _backend_fault;
    bool _counter_exhausted;
    bool _host_pending;
    bool _abort_pending;
    cf09_a_session_id_t _session;
    cf09_a_backend_ops_t _backend;
    uint32_t _resource_generation[CF09_A_EXTERNAL_TX_COUNT];
    uint32_t _configured_generation[CF09_A_EXTERNAL_TX_COUNT];
    uint32_t _idle_generation[CF09_A_EXTERNAL_TX_COUNT];
    bool _configured[CF09_A_EXTERNAL_TX_COUNT];
    bool _idle[CF09_A_EXTERNAL_TX_COUNT];
    bool _frame_seen[CF09_A_EXTERNAL_TX_COUNT];
    cf09_a_frame_id_t _last_frame[CF09_A_EXTERNAL_TX_COUNT];
    cf09_a_attempt_slot_t _slots[CF09_A_EXTERNAL_TX_COUNT];
    cf09_a_attempt_token_t _host_pending_token;
    uint32_t _bus_generation;
    uint32_t _device_generation;
    uint64_t _last_transfer_no;
    uint64_t _completed_count;
    uint8_t _completed_route_mask;
    uint64_t _uncertain_count;
    uint64_t _rejected_callback_count;
} cf09_a_driver_t;

typedef struct {
    cf09_a_model_status_t model_status;
    uint64_t completed_count;
    uint8_t completed_route_mask;
    uint64_t uncertain_count;
    uint64_t rejected_callback_count;
    size_t open_attempts;
    size_t uncertain_attempts;
    bool outputs_armed;
    bool bridge_up;
    bool reset_active;
    cf09_a_completion_path_health_t completion_path_health;
    const char *model_class;
    const char *hardware_status;
    const char *exact_part_status;
    const char *connector_delivery_status;
    const char *completion_scope;
} cf09_a_validation_t;

cf09_a_result_t cf09_a_driver_init(
    cf09_a_driver_t *driver,
    cf09_a_session_id_t session,
    const cf09_a_backend_ops_t *backend);

/*
 * Two-phase shutdown: disable/abort, then backend quiesce/drain.  On any
 * failure the context and token are retained so the caller can retry deinit;
 * storage may be released only after this function returns OK or UNCERTAIN.
 */
cf09_a_result_t cf09_a_driver_deinit(cf09_a_driver_t *driver);

const cf09_a_route_t *cf09_a_route_for_logical(uint8_t logical_channel);

bool cf09_a_token_equal(
    const cf09_a_attempt_token_t *left,
    const cf09_a_attempt_token_t *right);

uint64_t cf09_a_payload_fingerprint(
    const uint8_t *payload,
    size_t payload_len);

/*
 * Caller attestation only: invoke after bus/device probe, stale FIFO flush and
 * IRQ/status clear succeed in the exact-part backend.  It is not a probe.
 */
cf09_a_result_t cf09_a_confirm_bridge_up(cf09_a_driver_t *driver);
cf09_a_result_t cf09_a_release_reset(cf09_a_driver_t *driver);

cf09_a_result_t cf09_a_mark_resource_configured(
    cf09_a_driver_t *driver,
    uint8_t physical_channel,
    uint32_t generation);

cf09_a_result_t cf09_a_mark_resource_idle(
    cf09_a_driver_t *driver,
    uint8_t physical_channel,
    uint32_t generation);

cf09_a_result_t cf09_a_arm_outputs(cf09_a_driver_t *driver);

cf09_a_result_t cf09_a_begin_tx(
    cf09_a_driver_t *driver,
    const cf09_a_tx_request_t *request,
    cf09_a_attempt_token_t *out_token);

cf09_a_result_t cf09_a_retry_not_committed(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *previous_token,
    const uint8_t *payload,
    size_t payload_len,
    cf09_a_attempt_token_t *out_token);

/* Explicitly abandon a frame proven not committed; no delivery is inferred. */
cf09_a_result_t cf09_a_discard_not_committed(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token);

cf09_a_result_t cf09_a_on_host_result(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result);

cf09_a_result_t cf09_a_on_serializer_done_latched(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token);

cf09_a_result_t cf09_a_confirm_serializer_complete(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t source);

cf09_a_result_t cf09_a_on_completion_timeout(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token);

cf09_a_result_t cf09_a_on_device_reset(cf09_a_driver_t *driver);
cf09_a_result_t cf09_a_on_bus_reset(cf09_a_driver_t *driver);

cf09_a_result_t cf09_a_reap_complete(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token);

/* Frees the slot only; lifetime uncertainty evidence remains latched. */
cf09_a_result_t cf09_a_reconcile_uncertain(
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token);

cf09_a_result_t cf09_a_get_attempt_state(
    const cf09_a_driver_t *driver,
    uint8_t logical_channel,
    cf09_a_attempt_state_t *out_state);

cf09_a_result_t cf09_a_get_resource_generation(
    const cf09_a_driver_t *driver,
    uint8_t physical_channel,
    uint32_t *out_generation);

cf09_a_validation_t cf09_a_validate(const cf09_a_driver_t *driver);

const char *cf09_a_result_label(cf09_a_result_t result);
const char *cf09_a_model_status_label(cf09_a_model_status_t status);

#ifdef __cplusplus
}
#endif

#endif
