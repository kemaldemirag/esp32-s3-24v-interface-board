#include "cf09_max14830_timing_contract_v1.h"

#include <limits.h>
#include <string.h>

enum {
    /* MAX14830 Rev. 7 absolute fREF limit. */
    CF09_MAX14830_TIMING_REFERENCE_HZ_MAX = 96000000,
    CF09_MAX14830_TIMING_BAUD_STANDARD_MAX = 6000000,
    CF09_MAX14830_TIMING_BAUD_2X_MAX = 12000000,
    CF09_MAX14830_TIMING_BAUD_4X_MAX = 24000000
};

static bool cf09_max14830_timing_mul_u64(
    uint64_t left,
    uint64_t right,
    uint64_t *product_out)
{
    if (product_out == NULL ||
        (right != 0U && left > UINT64_MAX / right)) {
        return false;
    }
    *product_out = left * right;
    return true;
}

static bool cf09_max14830_timing_ceil_div_u64(
    uint64_t numerator,
    uint64_t denominator,
    uint64_t *quotient_out)
{
    uint64_t quotient;

    if (quotient_out == NULL || numerator == 0U || denominator == 0U) {
        return false;
    }
    quotient = numerator / denominator;
    if (numerator % denominator != 0U) {
        if (quotient == UINT64_MAX) {
            return false;
        }
        quotient++;
    }
    *quotient_out = quotient;
    return true;
}

static bool cf09_max14830_timing_mode(
    uint8_t brg_config,
    uint8_t *oversampling_divisor_out,
    uint64_t *baud_limit_out)
{
    bool mode_2x;
    bool mode_4x;

    if (oversampling_divisor_out == NULL || baud_limit_out == NULL ||
        (brg_config &
         (uint8_t)CF09_MAX14830_TIMING_BRG_RESERVED_MASK) != 0U) {
        return false;
    }
    mode_2x = (brg_config &
               (uint8_t)CF09_MAX14830_TIMING_BRG_2X_BIT) != 0U;
    mode_4x = (brg_config &
               (uint8_t)CF09_MAX14830_TIMING_BRG_4X_BIT) != 0U;
    if (mode_2x && mode_4x) {
        return false;
    }
    if (mode_4x) {
        *oversampling_divisor_out = 4U;
        *baud_limit_out =
            (uint64_t)CF09_MAX14830_TIMING_BAUD_4X_MAX;
    } else if (mode_2x) {
        *oversampling_divisor_out = 8U;
        *baud_limit_out =
            (uint64_t)CF09_MAX14830_TIMING_BAUD_2X_MAX;
    } else {
        *oversampling_divisor_out = 16U;
        *baud_limit_out =
            (uint64_t)CF09_MAX14830_TIMING_BAUD_STANDARD_MAX;
    }
    return true;
}

static void cf09_max14830_timing_decode_frame(
    uint8_t lcr,
    cf09_max14830_timing_derived_t *derived)
{
    bool two_or_one_half_stop;

    derived->data_bits =
        (uint8_t)(5U +
                  (lcr &
                   (uint8_t)CF09_MAX14830_TIMING_LCR_LENGTH_MASK));
    derived->parity_enabled =
        (lcr &
         (uint8_t)CF09_MAX14830_TIMING_LCR_PARITY_ENABLE_BIT) != 0U;
    two_or_one_half_stop =
        (lcr &
         (uint8_t)CF09_MAX14830_TIMING_LCR_STOP_BITS_BIT) != 0U;
    if (!two_or_one_half_stop) {
        derived->stop_half_bits = 2U;
    } else if (derived->data_bits == 5U) {
        derived->stop_half_bits = 3U;
    } else {
        derived->stop_half_bits = 4U;
    }
    derived->frame_half_bits = (uint8_t)(
        2U + (uint8_t)(2U * derived->data_bits) +
        (derived->parity_enabled ? 2U : 0U) +
        derived->stop_half_bits);
}

cf09_max14830_timing_result_t cf09_max14830_timing_derive_guard(
    const cf09_max14830_timing_config_t *config,
    cf09_max14830_timing_derived_t *derived_out)
{
    cf09_max14830_timing_config_t config_copy;
    cf09_max14830_timing_derived_t derived;
    uint64_t baud_limit;
    uint64_t product;
    uint64_t guard_numerator;
    uint64_t guard_denominator;
    uint64_t baud_upper_left;
    uint64_t baud_upper_right;
    uint8_t fraction;

    if (config == NULL || derived_out == NULL) {
        if (derived_out != NULL) {
            memset(derived_out, 0, sizeof(*derived_out));
        }
        return CF09_MAX14830_TIMING_INVALID_ARGUMENT;
    }
    /* Snapshot before clearing output so overlapping caller storage is safe. */
    config_copy = *config;
    config = &config_copy;
    memset(derived_out, 0, sizeof(*derived_out));
    memset(&derived, 0, sizeof(derived));
    if (config->profile !=
            CF09_MAX14830_TIMING_PROFILE_NORMAL_ASYNC_UART ||
        config->scope !=
            CF09_MAX14830_TIMING_SCOPE_SERIALIZER_COMPLETE) {
        return CF09_MAX14830_TIMING_UNSUPPORTED_MODE;
    }
    if (config->divisor_integer == 0U ||
        config->reference_hz_lower_bound == 0U ||
        config->reference_hz_upper_bound == 0U ||
        config->timer_hz_upper_bound == 0U ||
        config->reference_hz_lower_bound >
            config->reference_hz_upper_bound) {
        return CF09_MAX14830_TIMING_INVALID_CONFIGURATION;
    }
    if (config->logical_channel < 2U || config->logical_channel > 4U ||
        config->uart > 2U ||
        config->uart != (uint8_t)(config->logical_channel - 2U)) {
        return CF09_MAX14830_TIMING_INVALID_CONFIGURATION;
    }
    if (config->configuration_generation == 0U) {
        return CF09_MAX14830_TIMING_INVALID_CONFIGURATION;
    }
    if (config->configuration_generation == UINT32_MAX) {
        return CF09_MAX14830_TIMING_COUNTER_EXHAUSTED;
    }
    if (config->reference_hz_upper_bound >
        (uint64_t)CF09_MAX14830_TIMING_REFERENCE_HZ_MAX) {
        return CF09_MAX14830_TIMING_OUTSIDE_DATASHEET_LIMIT;
    }
    if ((config->lcr &
         (uint8_t)CF09_MAX14830_TIMING_LCR_TX_BREAK_BIT) != 0U) {
        return CF09_MAX14830_TIMING_UNSUPPORTED_MODE;
    }
    if ((config->brg_config &
         (uint8_t)CF09_MAX14830_TIMING_BRG_CLOCK_DISABLE_BIT) != 0U) {
        return CF09_MAX14830_TIMING_UNSUPPORTED_MODE;
    }
    if (!cf09_max14830_timing_mode(
            config->brg_config,
            &derived.oversampling_divisor,
            &baud_limit)) {
        return CF09_MAX14830_TIMING_INVALID_CONFIGURATION;
    }
    fraction = (uint8_t)(
        config->brg_config &
        (uint8_t)CF09_MAX14830_TIMING_BRG_FRACTION_MASK);
    if (fraction != 0U) {
        return CF09_MAX14830_TIMING_FRACTIONAL_UNBOUNDED;
    }

    cf09_max14830_timing_decode_frame(config->lcr, &derived);
    derived.config_snapshot = *config;
    derived.divisor_sixteenths =
        (uint32_t)config->divisor_integer * UINT32_C(16);

    /*
     * baud = 16*fREF / (S*q).  Reject a configuration whose upper
     * clock bound can exceed the Rev. 7 mode-specific baud limit.
     */
    if (!cf09_max14830_timing_mul_u64(
            UINT64_C(16),
            config->reference_hz_upper_bound,
            &baud_upper_left) ||
        !cf09_max14830_timing_mul_u64(
            (uint64_t)derived.oversampling_divisor,
            (uint64_t)derived.divisor_sixteenths,
            &product) ||
        !cf09_max14830_timing_mul_u64(
            baud_limit, product, &baud_upper_right)) {
        return CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW;
    }
    if (baud_upper_left > baud_upper_right) {
        return CF09_MAX14830_TIMING_OUTSIDE_DATASHEET_LIMIT;
    }

    if (!cf09_max14830_timing_mul_u64(
            UINT64_C(16),
            config->reference_hz_lower_bound,
            &derived.baud_lower_bound_numerator) ||
        !cf09_max14830_timing_mul_u64(
            UINT64_C(16),
            config->reference_hz_upper_bound,
            &derived.baud_upper_bound_numerator) ||
        !cf09_max14830_timing_mul_u64(
            (uint64_t)derived.oversampling_divisor,
            (uint64_t)derived.divisor_sixteenths,
            &derived.baud_bound_denominator)) {
        return CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW;
    }

    /*
     * longest character seconds = frame_halfbits*S*q/(32*fREF_min)
     * guard ticks = ceil(longest_character*timer_hz_max)
     */
    if (!cf09_max14830_timing_mul_u64(
            (uint64_t)derived.frame_half_bits,
            (uint64_t)derived.oversampling_divisor,
            &product) ||
        !cf09_max14830_timing_mul_u64(
            product,
            (uint64_t)derived.divisor_sixteenths,
            &derived.character_time_upper_bound_numerator) ||
        !cf09_max14830_timing_mul_u64(
            UINT64_C(32),
            config->reference_hz_lower_bound,
            &derived.character_time_upper_bound_denominator) ||
        !cf09_max14830_timing_mul_u64(
            derived.character_time_upper_bound_numerator,
            config->timer_hz_upper_bound,
            &guard_numerator)) {
        return CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW;
    }
    guard_denominator = derived.character_time_upper_bound_denominator;
    if (!cf09_max14830_timing_ceil_div_u64(
            guard_numerator,
            guard_denominator,
            &derived.guard_ticks_minimum)) {
        return CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW;
    }
    *derived_out = derived;
    return CF09_MAX14830_TIMING_OK;
}

static bool cf09_max14830_timing_plan_matches_token(
    const cf09_max14830_timing_derived_t *derived,
    const cf09_a_attempt_token_t *token,
    uint32_t current_configuration_generation)
{
    const cf09_max14830_timing_config_t *config;

    if (derived == NULL || token == NULL ||
        derived->guard_ticks_minimum == 0U) {
        return false;
    }
    config = &derived->config_snapshot;
    return token->logical_channel == config->logical_channel &&
           token->serializer_id == config->logical_channel &&
           token->resource_id == config->uart &&
           token->physical_channel == config->uart &&
           token->device_id == (uint8_t)CF09_A_DEVICE_ID &&
           token->chip_select_id == (uint8_t)CF09_A_CHIP_SELECT_ID &&
           token->resource_generation == config->resource_generation &&
           token->bus_generation == config->bus_generation &&
           token->device_generation == config->device_generation &&
           current_configuration_generation ==
               config->configuration_generation;
}

static bool cf09_max14830_timing_tokens_equal(
    const cf09_a_attempt_token_t *left,
    const cf09_a_attempt_token_t *right)
{
    return left != NULL && right != NULL &&
           left->session.high == right->session.high &&
           left->session.low == right->session.low &&
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

bool cf09_max14830_timing_bind_attempt(
    const cf09_max14830_timing_derived_t *derived,
    const cf09_a_attempt_token_t *token,
    uint32_t current_configuration_generation,
    cf09_max14830_timing_binding_t *binding_out)
{
    cf09_a_attempt_token_t token_copy;

    if (binding_out == NULL) {
        return false;
    }
    if (token == NULL) {
        memset(binding_out, 0, sizeof(*binding_out));
        return false;
    }
    token_copy = *token;
    memset(binding_out, 0, sizeof(*binding_out));
    if (!cf09_max14830_timing_plan_matches_token(
            derived, &token_copy, current_configuration_generation)) {
        return false;
    }
    binding_out->token = token_copy;
    binding_out->configuration_generation =
        current_configuration_generation;
    binding_out->valid = true;
    return true;
}

bool cf09_max14830_timing_binding_matches(
    const cf09_max14830_timing_derived_t *derived,
    const cf09_max14830_timing_binding_t *binding,
    const cf09_a_attempt_token_t *token,
    uint32_t current_configuration_generation)
{
    return binding != NULL && binding->valid &&
           binding->configuration_generation ==
               current_configuration_generation &&
           cf09_max14830_timing_plan_matches_token(
               derived, token, current_configuration_generation) &&
           cf09_max14830_timing_tokens_equal(&binding->token, token);
}

const char *cf09_max14830_timing_result_name(
    cf09_max14830_timing_result_t result)
{
    switch (result) {
    case CF09_MAX14830_TIMING_OK:
        return "OK";
    case CF09_MAX14830_TIMING_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case CF09_MAX14830_TIMING_INVALID_CONFIGURATION:
        return "INVALID_CONFIGURATION";
    case CF09_MAX14830_TIMING_UNSUPPORTED_MODE:
        return "UNSUPPORTED_MODE";
    case CF09_MAX14830_TIMING_FRACTIONAL_UNBOUNDED:
        return "FRACTIONAL_UNBOUNDED";
    case CF09_MAX14830_TIMING_OUTSIDE_DATASHEET_LIMIT:
        return "OUTSIDE_DATASHEET_LIMIT";
    case CF09_MAX14830_TIMING_COUNTER_EXHAUSTED:
        return "COUNTER_EXHAUSTED";
    case CF09_MAX14830_TIMING_ARITHMETIC_OVERFLOW:
        return "ARITHMETIC_OVERFLOW";
    default:
        return "INVALID_ENUM";
    }
}
