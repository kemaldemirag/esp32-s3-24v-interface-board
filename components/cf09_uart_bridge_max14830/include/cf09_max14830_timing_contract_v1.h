#ifndef CF09_MAX14830_TIMING_CONTRACT_V1_H
#define CF09_MAX14830_TIMING_CONTRACT_V1_H

/*
 * Fail-closed timing contract for the CF-09 MAX14830 candidate backend.
 *
 * Primary evidence:
 *   Analog Devices MAX14830 data sheet, Rev. 7 (2016), pp. 18, 21, 32,
 *   41 and 52:
 *   https://www.analog.com/media/en/technical-documentation/data-sheets/MAX14830.pdf
 *
 * Evidence boundary:
 *   - DERIVED / NATIVE_TEST_PASS for integer-BRG
 *     arithmetic only.
 *   - The caller supplies measured or otherwise qualified bounds.  No clock,
 *     baud, LCR, timer or margin value is defaulted here.
 *   - FRACT != 0 is rejected because Rev. 7 describes the fractional baud
 *     clock as irregular but supplies no per-character worst-case jitter
 *     bound.  Its average-rate equation is not a conservative guard proof.
 *   - Only normal asynchronous UART serializer completion is supported.
 *     IrDA, multidrop, loopback, TxBreak and external RTS/DE release are not
 *     covered.  In particular, HDplxDelay.Hold must be added separately when
 *     the required event is external transceiver release rather than the last
 *     UART stop bit.
 *   - ISR[5] (0x20) means the final FIFO word has started transmitting.
 *     ISR[6] (0x40) is RX-FIFO-empty and is not TX-completion evidence.
 *   - ESP-IDF binding, target timer behavior and hardware measurement remain
 *     NOT_RUN.  The selected configuration must remain immutable from TX
 *     admission through the fresh ISR[5] observation and guard expiry.
 */

#include "cf09_uart_bridge_v1.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF09_MAX14830_TIMING_MODEL_STATUS \
    "DERIVED / NATIVE_TEST_PASS / INTEGER_BRG_ONLY"
#define CF09_MAX14830_TIMING_HARDWARE_STATUS \
    "NOT_RUN / TIMER_AND_LOGIC_ANALYZER_REQUIRED"
#define CF09_MAX14830_TIMING_CONFIGURATION_STATUS \
    "CALLER_BOUNDED / NO_DEFAULT_CLOCK_OR_BAUD"
#define CF09_MAX14830_TIMING_FRACTIONAL_STATUS \
    "BLOCKED / WORST_CASE_CHARACTER_JITTER_NOT_SPECIFIED"
#define CF09_MAX14830_TIMING_SCOPE_STATUS \
    "UART_SERIALIZER_ONLY / EXTERNAL_RTS_DE_RELEASE_NOT_COVERED"

enum {
    CF09_MAX14830_TIMING_ISR_TX_FIFO_EMPTY_BIT = 0x20,
    CF09_MAX14830_TIMING_ISR_RX_FIFO_EMPTY_BIT = 0x40,

    CF09_MAX14830_TIMING_LCR_LENGTH_MASK = 0x03,
    CF09_MAX14830_TIMING_LCR_STOP_BITS_BIT = 0x04,
    CF09_MAX14830_TIMING_LCR_PARITY_ENABLE_BIT = 0x08,
    CF09_MAX14830_TIMING_LCR_TX_BREAK_BIT = 0x40,

    CF09_MAX14830_TIMING_BRG_FRACTION_MASK = 0x0F,
    CF09_MAX14830_TIMING_BRG_2X_BIT = 0x10,
    CF09_MAX14830_TIMING_BRG_4X_BIT = 0x20,
    CF09_MAX14830_TIMING_BRG_CLOCK_DISABLE_BIT = 0x40,
    CF09_MAX14830_TIMING_BRG_RESERVED_MASK = 0x80
};

typedef enum {
    CF09_MAX14830_TIMING_OK = 0,
    CF09_MAX14830_TIMING_INVALID_ARGUMENT,
    CF09_MAX14830_TIMING_INVALID_CONFIGURATION,
    CF09_MAX14830_TIMING_UNSUPPORTED_MODE,
    CF09_MAX14830_TIMING_FRACTIONAL_UNBOUNDED,
    CF09_MAX14830_TIMING_OUTSIDE_DATASHEET_LIMIT,
    CF09_MAX14830_TIMING_COUNTER_EXHAUSTED,
    CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW
} cf09_max14830_timing_result_t;

typedef enum {
    CF09_MAX14830_TIMING_PROFILE_INVALID = 0,
    /* Caller attests normal async UART; no IrDA, multidrop or loopback. */
    CF09_MAX14830_TIMING_PROFILE_NORMAL_ASYNC_UART = 1
} cf09_max14830_timing_profile_t;

typedef enum {
    CF09_MAX14830_TIMING_SCOPE_INVALID = 0,
    /* Completion means the final UART stop bit has elapsed. */
    CF09_MAX14830_TIMING_SCOPE_SERIALIZER_COMPLETE = 1
} cf09_max14830_timing_scope_t;

typedef struct {
    cf09_max14830_timing_profile_t profile;
    cf09_max14830_timing_scope_t scope;

    /* Same-generation binding for logical channels L2..L4 -> UART0..2. */
    uint8_t logical_channel;
    uint8_t uart;
    uint32_t resource_generation;
    uint32_t bus_generation;
    uint32_t device_generation;
    /*
     * Config owner starts at 1 and increments before any LCR/BRG/global-clock
     * write.  Zero and UINT32_MAX are rejected to prevent wrap reuse.
     */
    uint32_t configuration_generation;

    /* Combined 16-bit DIVMSB:DIVLSB value; Rev. 7 minimum is 1. */
    uint16_t divisor_integer;
    /* Raw BRGConfig: FRACT[3:0], 2xMode[4], 4xMode[5]. */
    uint8_t brg_config;
    /* Raw LCR.  TxBreak is rejected; parity value bits do not affect time. */
    uint8_t lcr;

    /* Qualified bounds for the stable MAX14830 reference clock. */
    uint64_t reference_hz_lower_bound;
    uint64_t reference_hz_upper_bound;
    /* Qualified upper bound on actual timer ticks per second (early bound). */
    uint64_t timer_hz_upper_bound;
} cf09_max14830_timing_config_t;

typedef struct {
    /* Exact immutable input snapshot to which this plan is bound. */
    cf09_max14830_timing_config_t config_snapshot;
    uint8_t data_bits;
    bool parity_enabled;
    uint8_t stop_half_bits;
    uint8_t frame_half_bits;
    uint8_t oversampling_divisor;
    uint32_t divisor_sixteenths;

    /* Baud bounds are exact rationals: numerator / denominator. */
    uint64_t baud_lower_bound_numerator;
    uint64_t baud_upper_bound_numerator;
    uint64_t baud_bound_denominator;

    /* Longest-character bound is exact seconds: numerator / denominator. */
    uint64_t character_time_upper_bound_numerator;
    uint64_t character_time_upper_bound_denominator;

    /*
     * Minimum integer timer ticks under the caller's supplied bounds.
     * This includes no invented margin.  It is valid only while every input
     * register and clock bound remains valid for the observed ISR[5] event.
     */
    uint64_t guard_ticks_minimum;
} cf09_max14830_timing_derived_t;

typedef struct {
    bool valid;
    cf09_a_attempt_token_t token;
    uint32_t configuration_generation;
} cf09_max14830_timing_binding_t;

cf09_max14830_timing_result_t cf09_max14830_timing_derive_guard(
    const cf09_max14830_timing_config_t *config,
    cf09_max14830_timing_derived_t *derived_out);

/*
 * Called at TX admission, before serialization starts.  Captures the complete
 * attempt token and the plan's configuration generation.  Configuration
 * writes are forbidden while a valid binding is active.
 */
bool cf09_max14830_timing_bind_attempt(
    const cf09_max14830_timing_derived_t *derived,
    const cf09_a_attempt_token_t *token,
    uint32_t current_configuration_generation,
    cf09_max14830_timing_binding_t *binding_out);

/* Rejects stale/replaced attempts and any config change before guard arm. */
bool cf09_max14830_timing_binding_matches(
    const cf09_max14830_timing_derived_t *derived,
    const cf09_max14830_timing_binding_t *binding,
    const cf09_a_attempt_token_t *token,
    uint32_t current_configuration_generation);

const char *cf09_max14830_timing_result_name(
    cf09_max14830_timing_result_t result);

#ifdef __cplusplus
}
#endif

#endif
