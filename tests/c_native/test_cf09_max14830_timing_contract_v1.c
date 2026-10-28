#include "cf09_max14830_timing_contract_v1.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                     \
        }                                                                     \
    } while (0)

typedef bool (*cf09_timing_test_fn)(void);

typedef struct {
    const char *name;
    cf09_timing_test_fn run;
} cf09_timing_test_case_t;

static cf09_max14830_timing_config_t base_config(void)
{
    cf09_max14830_timing_config_t config;

    memset(&config, 0, sizeof(config));
    config.profile = CF09_MAX14830_TIMING_PROFILE_NORMAL_ASYNC_UART;
    config.scope = CF09_MAX14830_TIMING_SCOPE_SERIALIZER_COMPLETE;
    config.logical_channel = 2U;
    config.uart = 0U;
    config.resource_generation = 11U;
    config.bus_generation = 22U;
    config.device_generation = 33U;
    config.configuration_generation = 44U;
    config.divisor_integer = 1U;
    config.brg_config = 0U;
    config.lcr = 0x03U; /* 8N1; arithmetic test vector, not a requirement. */
    config.reference_hz_lower_bound = UINT64_C(16000000);
    config.reference_hz_upper_bound = UINT64_C(16000000);
    config.timer_hz_upper_bound = UINT64_C(1000000);
    return config;
}

static bool test_evidence_boundary_and_isr_identity(void)
{
    CHECK(strcmp(CF09_MAX14830_TIMING_MODEL_STATUS,
                 "DERIVED / NATIVE_TEST_PASS / INTEGER_BRG_ONLY") == 0);
    CHECK(strcmp(CF09_MAX14830_TIMING_HARDWARE_STATUS,
                 "NOT_RUN / TIMER_AND_LOGIC_ANALYZER_REQUIRED") == 0);
    CHECK(strcmp(CF09_MAX14830_TIMING_CONFIGURATION_STATUS,
                 "CALLER_BOUNDED / NO_DEFAULT_CLOCK_OR_BAUD") == 0);
    CHECK(strcmp(CF09_MAX14830_TIMING_FRACTIONAL_STATUS,
                 "BLOCKED / WORST_CASE_CHARACTER_JITTER_NOT_SPECIFIED") ==
          0);
    CHECK(strcmp(CF09_MAX14830_TIMING_SCOPE_STATUS,
                 "UART_SERIALIZER_ONLY / "
                 "EXTERNAL_RTS_DE_RELEASE_NOT_COVERED") == 0);
    CHECK((uint8_t)CF09_MAX14830_TIMING_ISR_TX_FIFO_EMPTY_BIT == 0x20U);
    CHECK((uint8_t)CF09_MAX14830_TIMING_ISR_RX_FIFO_EMPTY_BIT == 0x40U);
    return true;
}

static bool test_8n1_integer_guard_is_exact(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.data_bits == 8U);
    CHECK(!derived.parity_enabled);
    CHECK(derived.stop_half_bits == 2U);
    CHECK(derived.frame_half_bits == 20U);
    CHECK(derived.oversampling_divisor == 16U);
    CHECK(derived.divisor_sixteenths == 16U);
    CHECK(derived.baud_lower_bound_numerator == UINT64_C(256000000));
    CHECK(derived.baud_upper_bound_numerator == UINT64_C(256000000));
    CHECK(derived.baud_bound_denominator == UINT64_C(256));
    CHECK(derived.character_time_upper_bound_numerator ==
          UINT64_C(5120));
    CHECK(derived.character_time_upper_bound_denominator ==
          UINT64_C(512000000));
    CHECK(derived.guard_ticks_minimum == UINT64_C(10));
    return true;
}

static bool test_lcr_half_bit_formats(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.lcr = 0x04U; /* 5 data, no parity, 1.5 stop. */
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.data_bits == 5U);
    CHECK(derived.stop_half_bits == 3U);
    CHECK(derived.frame_half_bits == 15U);

    config.lcr = 0x0EU; /* 7 data, parity, 2 stop. */
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.data_bits == 7U);
    CHECK(derived.parity_enabled);
    CHECK(derived.stop_half_bits == 4U);
    CHECK(derived.frame_half_bits == 22U);
    CHECK(derived.guard_ticks_minimum == UINT64_C(11));
    return true;
}

static bool test_all_word_lengths_and_stop_mapping(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;
    uint8_t length;
    uint8_t parity;

    for (parity = 0U; parity < 2U; ++parity) {
        for (length = 0U; length < 4U; ++length) {
            config.lcr = (uint8_t)(
                length |
                (parity != 0U ?
                    (uint8_t)CF09_MAX14830_TIMING_LCR_PARITY_ENABLE_BIT :
                    0U));
            CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
                  CF09_MAX14830_TIMING_OK);
            CHECK(derived.data_bits == (uint8_t)(5U + length));
            CHECK(derived.parity_enabled == (parity != 0U));
            CHECK(derived.stop_half_bits == 2U);
            CHECK(derived.frame_half_bits == (uint8_t)(
                2U + 2U * (5U + length) + 2U +
                (parity != 0U ? 2U : 0U)));
            config.lcr = (uint8_t)(
                config.lcr |
                (uint8_t)CF09_MAX14830_TIMING_LCR_STOP_BITS_BIT);
            CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
                  CF09_MAX14830_TIMING_OK);
            CHECK(derived.stop_half_bits ==
                  (length == 0U ? 3U : 4U));
            CHECK(derived.frame_half_bits == (uint8_t)(
                2U + 2U * (5U + length) +
                (length == 0U ? 3U : 4U) +
                (parity != 0U ? 2U : 0U)));
        }
    }
    return true;
}

static bool test_source_backed_reset_lcr_is_not_8n1(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.lcr = 0x05U; /* Rev. 7 reset: 6 data, no parity, 2 stop. */
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.data_bits == 6U);
    CHECK(!derived.parity_enabled);
    CHECK(derived.stop_half_bits == 4U);
    CHECK(derived.frame_half_bits == 18U);
    return true;
}

static bool test_2x_and_4x_modes_change_guard(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.brg_config =
        (uint8_t)CF09_MAX14830_TIMING_BRG_2X_BIT;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.oversampling_divisor == 8U);
    CHECK(derived.guard_ticks_minimum == UINT64_C(5));

    config.brg_config =
        (uint8_t)CF09_MAX14830_TIMING_BRG_4X_BIT;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.oversampling_divisor == 4U);
    CHECK(derived.guard_ticks_minimum == UINT64_C(3));
    return true;
}

static bool test_ceiling_never_under_waits(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.timer_hz_upper_bound = UINT64_C(1000001);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.guard_ticks_minimum == UINT64_C(11));
    return true;
}

static bool test_qualified_clock_bounds_are_conservative(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.reference_hz_lower_bound = UINT64_C(15999999);
    config.reference_hz_upper_bound = UINT64_C(16000001);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(derived.guard_ticks_minimum == UINT64_C(11));
    CHECK(derived.baud_lower_bound_numerator == UINT64_C(255999984));
    CHECK(derived.baud_upper_bound_numerator == UINT64_C(256000016));
    return true;
}

static bool test_fractional_brg_is_blocked_without_worst_case_bound(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_derived_t zero;
    uint8_t fraction;

    memset(&zero, 0, sizeof(zero));
    for (fraction = 1U; fraction <= 15U; ++fraction) {
        memset(&derived, 0xA5, sizeof(derived));
        config.brg_config = fraction;
        CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
              CF09_MAX14830_TIMING_FRACTIONAL_UNBOUNDED);
        CHECK(memcmp(&derived, &zero, sizeof(derived)) == 0);
    }
    return true;
}

static bool test_invalid_modes_and_txbreak_fail_closed(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.brg_config = (uint8_t)(
        (uint8_t)CF09_MAX14830_TIMING_BRG_2X_BIT |
        (uint8_t)CF09_MAX14830_TIMING_BRG_4X_BIT);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config.brg_config =
        (uint8_t)CF09_MAX14830_TIMING_BRG_RESERVED_MASK;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config.brg_config =
        (uint8_t)CF09_MAX14830_TIMING_BRG_CLOCK_DISABLE_BIT;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_UNSUPPORTED_MODE);
    config.brg_config = 0U;
    config.lcr = (uint8_t)(
        0x03U | (uint8_t)CF09_MAX14830_TIMING_LCR_TX_BREAK_BIT);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_UNSUPPORTED_MODE);
    return true;
}

static bool test_generation_bound_plan_rejects_stale_or_misrouted_token(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_binding_t binding;
    cf09_a_attempt_token_t token;

    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    memset(&token, 0, sizeof(token));
    token.logical_channel = config.logical_channel;
    token.serializer_id = config.logical_channel;
    token.resource_id = config.uart;
    token.physical_channel = config.uart;
    token.device_id = (uint8_t)CF09_A_DEVICE_ID;
    token.chip_select_id = (uint8_t)CF09_A_CHIP_SELECT_ID;
    token.resource_generation = config.resource_generation;
    token.bus_generation = config.bus_generation;
    token.device_generation = config.device_generation;
    token.session.high = 1U;
    token.session.low = 2U;
    token.frame.epoch = 3U;
    token.frame.sequence = 4U;
    token.attempt_no = 5U;
    token.transfer_no = 6U;
    token.payload_len = 7U;
    token.payload_fingerprint = 8U;
    CHECK(cf09_max14830_timing_bind_attempt(
        &derived, &token, config.configuration_generation, &binding));
    CHECK(cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));

    token.session.high++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.session.high--;
    token.session.low++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.session.low--;
    token.frame.epoch++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.frame.epoch--;
    token.frame.sequence++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.frame.sequence--;
    token.transfer_no++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.transfer_no--;
    token.payload_len++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.payload_len--;
    token.payload_fingerprint++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.payload_fingerprint--;
    token.device_generation++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.device_generation = config.device_generation;
    token.physical_channel = 1U;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.physical_channel = config.uart;
    token.logical_channel++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.logical_channel = config.logical_channel;
    token.serializer_id++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.serializer_id = config.logical_channel;
    token.resource_id++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.resource_id = config.uart;
    token.device_id++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.device_id = (uint8_t)CF09_A_DEVICE_ID;
    token.chip_select_id++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.chip_select_id = (uint8_t)CF09_A_CHIP_SELECT_ID;
    token.resource_generation++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.resource_generation = config.resource_generation;
    token.bus_generation++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.bus_generation = config.bus_generation;
    token.attempt_no++;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation));
    token.attempt_no--;
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, &token, config.configuration_generation + 1U));
    CHECK(!cf09_max14830_timing_binding_matches(
        NULL, &binding, &token, config.configuration_generation));
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, NULL, &token, config.configuration_generation));
    CHECK(!cf09_max14830_timing_binding_matches(
        &derived, &binding, NULL, config.configuration_generation));
    CHECK(!cf09_max14830_timing_bind_attempt(
        &derived, &token, config.configuration_generation, NULL));

    CHECK(!cf09_max14830_timing_bind_attempt(
        &derived, &token, config.configuration_generation + 1U,
        &binding));
    CHECK(!binding.valid);
    CHECK(binding.configuration_generation == 0U);
    CHECK(binding.token.logical_channel == 0U);
    CHECK(cf09_max14830_timing_bind_attempt(
        &derived, &token, config.configuration_generation, &binding));
    CHECK(binding.valid);
    CHECK(!cf09_max14830_timing_bind_attempt(
        &derived, NULL, config.configuration_generation, &binding));
    CHECK(!binding.valid);
    CHECK(binding.configuration_generation == 0U);
    CHECK(binding.token.logical_channel == 0U);
    return true;
}

static bool test_all_three_route_bindings(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_binding_t binding;
    cf09_a_attempt_token_t token;
    uint8_t logical;

    for (logical = 2U; logical <= 4U; ++logical) {
        config.logical_channel = logical;
        config.uart = (uint8_t)(logical - 2U);
        CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
              CF09_MAX14830_TIMING_OK);
        memset(&token, 0, sizeof(token));
        token.logical_channel = logical;
        token.serializer_id = logical;
        token.resource_id = config.uart;
        token.physical_channel = config.uart;
        token.device_id = (uint8_t)CF09_A_DEVICE_ID;
        token.chip_select_id = (uint8_t)CF09_A_CHIP_SELECT_ID;
        token.resource_generation = config.resource_generation;
        token.bus_generation = config.bus_generation;
        token.device_generation = config.device_generation;
        CHECK(cf09_max14830_timing_bind_attempt(
            &derived, &token, config.configuration_generation, &binding));
        CHECK(cf09_max14830_timing_binding_matches(
            &derived, &binding, &token, config.configuration_generation));
    }
    return true;
}

static bool test_overlapping_input_output_is_snapshotted_first(void)
{
    union {
        cf09_max14830_timing_config_t config;
        cf09_max14830_timing_derived_t derived;
    } storage;

    storage.config = base_config();
    CHECK(cf09_max14830_timing_derive_guard(
              &storage.config, &storage.derived) ==
          CF09_MAX14830_TIMING_OK);
    CHECK(storage.derived.config_snapshot.logical_channel == 2U);
    CHECK(storage.derived.guard_ticks_minimum == UINT64_C(10));
    return true;
}

static bool test_no_implicit_profile_scope_or_clock_defaults(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_derived_t zero;

    memset(&zero, 0, sizeof(zero));

    config.profile = CF09_MAX14830_TIMING_PROFILE_INVALID;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_UNSUPPORTED_MODE);
    config = base_config();
    config.scope = CF09_MAX14830_TIMING_SCOPE_INVALID;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_UNSUPPORTED_MODE);
    config = base_config();
    config.divisor_integer = 0U;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config = base_config();
    config.uart = 1U;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config = base_config();
    config.reference_hz_lower_bound = 0U;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config = base_config();
    config.timer_hz_upper_bound = 0U;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config = base_config();
    config.configuration_generation = 0U;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    config = base_config();
    config.configuration_generation = UINT32_MAX - 1U;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    config = base_config();
    config.configuration_generation = UINT32_MAX;
    memset(&derived, 0xA5, sizeof(derived));
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_COUNTER_EXHAUSTED);
    CHECK(memcmp(&derived, &zero, sizeof(derived)) == 0);
    return true;
}

static bool test_datasheet_limits_are_enforced(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    config.reference_hz_upper_bound = UINT64_C(96000001);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OUTSIDE_DATASHEET_LIMIT);

    config = base_config();
    config.reference_hz_lower_bound = UINT64_C(96000000);
    config.reference_hz_upper_bound = UINT64_C(96000000);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    config.brg_config =
        (uint8_t)CF09_MAX14830_TIMING_BRG_2X_BIT;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    config.brg_config =
        (uint8_t)CF09_MAX14830_TIMING_BRG_4X_BIT;
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_OK);
    return true;
}

static bool test_arithmetic_overflow_is_atomic(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;
    cf09_max14830_timing_derived_t zero;

    memset(&zero, 0, sizeof(zero));
    config.timer_hz_upper_bound = UINT64_MAX;
    memset(&derived, 0xA5, sizeof(derived));
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW);
    CHECK(memcmp(&derived, &zero, sizeof(derived)) == 0);
    return true;
}

static bool test_nulls_reversed_bounds_and_result_names(void)
{
    cf09_max14830_timing_config_t config = base_config();
    cf09_max14830_timing_derived_t derived;

    CHECK(cf09_max14830_timing_derive_guard(NULL, &derived) ==
          CF09_MAX14830_TIMING_INVALID_ARGUMENT);
    CHECK(cf09_max14830_timing_derive_guard(&config, NULL) ==
          CF09_MAX14830_TIMING_INVALID_ARGUMENT);
    config.reference_hz_lower_bound = UINT64_C(2);
    config.reference_hz_upper_bound = UINT64_C(1);
    CHECK(cf09_max14830_timing_derive_guard(&config, &derived) ==
          CF09_MAX14830_TIMING_INVALID_CONFIGURATION);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_OK),
                 "OK") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_FRACTIONAL_UNBOUNDED),
                 "FRACTIONAL_UNBOUNDED") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_INVALID_ARGUMENT),
                 "INVALID_ARGUMENT") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_INVALID_CONFIGURATION),
                 "INVALID_CONFIGURATION") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_UNSUPPORTED_MODE),
                 "UNSUPPORTED_MODE") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_OUTSIDE_DATASHEET_LIMIT),
                 "OUTSIDE_DATASHEET_LIMIT") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_COUNTER_EXHAUSTED),
                 "COUNTER_EXHAUSTED") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW),
                 "ARITHMETIC_OVERFLOW") == 0);
    CHECK(strcmp(cf09_max14830_timing_result_name(
                     (cf09_max14830_timing_result_t)99),
                 "INVALID_ENUM") == 0);
    return true;
}

int main(void)
{
    static const cf09_timing_test_case_t tests[] = {
        {"evidence_boundary_and_isr_identity",
         test_evidence_boundary_and_isr_identity},
        {"8n1_integer_guard_is_exact", test_8n1_integer_guard_is_exact},
        {"lcr_half_bit_formats", test_lcr_half_bit_formats},
        {"all_word_lengths_and_stop_mapping",
         test_all_word_lengths_and_stop_mapping},
        {"source_backed_reset_lcr_is_not_8n1",
         test_source_backed_reset_lcr_is_not_8n1},
        {"2x_and_4x_modes_change_guard",
         test_2x_and_4x_modes_change_guard},
        {"ceiling_never_under_waits", test_ceiling_never_under_waits},
        {"qualified_clock_bounds_are_conservative",
         test_qualified_clock_bounds_are_conservative},
        {"fractional_brg_is_blocked_without_worst_case_bound",
         test_fractional_brg_is_blocked_without_worst_case_bound},
        {"invalid_modes_and_txbreak_fail_closed",
         test_invalid_modes_and_txbreak_fail_closed},
        {"generation_bound_plan_rejects_stale_or_misrouted_token",
         test_generation_bound_plan_rejects_stale_or_misrouted_token},
        {"all_three_route_bindings", test_all_three_route_bindings},
        {"overlapping_input_output_is_snapshotted_first",
         test_overlapping_input_output_is_snapshotted_first},
        {"no_implicit_profile_scope_or_clock_defaults",
         test_no_implicit_profile_scope_or_clock_defaults},
        {"datasheet_limits_are_enforced",
         test_datasheet_limits_are_enforced},
        {"arithmetic_overflow_is_atomic",
         test_arithmetic_overflow_is_atomic},
        {"nulls_reversed_bounds_and_result_names",
         test_nulls_reversed_bounds_and_result_names}
    };
    size_t index;
    size_t passed = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            (void)printf("CF09 MAX14830 timing test failed: %s\n",
                         tests[index].name);
            return 1;
        }
        passed++;
    }
    (void)printf(
        "CF09 MAX14830 timing tests: %zu/%zu PASS "
        "(INTEGER_BRG_ONLY; hardware/timer NOT_RUN)\n",
        passed,
        sizeof(tests) / sizeof(tests[0]));
    return 0;
}
