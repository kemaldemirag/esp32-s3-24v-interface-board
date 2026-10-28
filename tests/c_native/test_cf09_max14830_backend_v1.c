#include "cf09_max14830_backend_fake_bus_v1.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef bool (*cf09_backend_test_fn)(void);

typedef struct {
    const char *name;
    cf09_backend_test_fn run;
} cf09_backend_test_case_t;

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) {                                                \
            (void)fprintf(stderr,                                           \
                          "CHECK failed at %s:%d: %s\n",                   \
                          __FILE__, __LINE__, #expression);                 \
            return false;                                                   \
        }                                                                   \
    } while (0)

static bool init_backend(
    cf09_max14830_backend_fake_bus_t *fake,
    cf09_max14830_backend_t *backend)
{
    cf09_max14830_backend_platform_ops_t platform;

    memset(backend, 0, sizeof(*backend));
    cf09_max14830_backend_fake_bus_init(fake);
    platform = cf09_max14830_backend_fake_bus_ops(fake);
    return cf09_max14830_backend_init(backend, &platform) ==
           CF09_MAX14830_BACKEND_OK;
}

static bool init_and_probe(
    cf09_max14830_backend_fake_bus_t *fake,
    cf09_max14830_backend_t *backend)
{
    return init_backend(fake, backend) &&
           cf09_max14830_backend_reset_and_probe(backend) ==
               CF09_MAX14830_BACKEND_OK;
}

static bool attest_all_and_enable(
    cf09_max14830_backend_t *backend)
{
    cf09_a_backend_ops_t ops = cf09_max14830_backend_core_ops(backend);
    uint8_t logical;

    for (logical = (uint8_t)CF09_A_EXTERNAL_LOGICAL_FIRST;
         logical <= (uint8_t)CF09_A_EXTERNAL_LOGICAL_LAST;
         ++logical) {
        size_t index = (size_t)(logical -
                                (uint8_t)CF09_A_EXTERNAL_LOGICAL_FIRST);
        uint32_t generation = backend->_resource_generation[index];
        if (cf09_max14830_backend_attest_channel_configured(
                backend, logical, generation) !=
                CF09_MAX14830_BACKEND_OK ||
            cf09_max14830_backend_verify_channel_idle(
                backend, logical, generation) !=
                CF09_MAX14830_BACKEND_OK) {
            return false;
        }
    }
    if (cf09_max14830_backend_attest_uart3_irq_masked(
            backend, backend->_device_generation) !=
        CF09_MAX14830_BACKEND_OK) {
        return false;
    }
    return ops.set_outputs_enabled(ops.user, true);
}

static cf09_a_attempt_token_t make_token(
    const cf09_max14830_backend_t *backend,
    uint8_t logical,
    uint32_t sequence,
    uint64_t transfer_no,
    const uint8_t *payload,
    size_t payload_len)
{
    cf09_a_attempt_token_t token;
    size_t index = (size_t)(logical -
                            (uint8_t)CF09_A_EXTERNAL_LOGICAL_FIRST);

    memset(&token, 0, sizeof(token));
    token.session.high = UINT64_C(0x1122334455667788);
    token.session.low = UINT64_C(0x8877665544332211);
    token.frame.epoch = 1U;
    token.frame.sequence = sequence;
    token.attempt_no = 1U;
    token.transfer_no = transfer_no;
    token.logical_channel = logical;
    token.serializer_id = logical;
    token.resource_id = (uint8_t)index;
    token.physical_channel = (uint8_t)index;
    token.device_id = (uint8_t)CF09_A_DEVICE_ID;
    token.chip_select_id = (uint8_t)CF09_A_CHIP_SELECT_ID;
    token.resource_generation = backend->_resource_generation[index];
    token.bus_generation = backend->_bus_generation;
    token.device_generation = backend->_device_generation;
    token.payload_len = payload_len;
    token.payload_fingerprint =
        cf09_a_payload_fingerprint(payload, payload_len);
    return token;
}

static bool start_and_commit(
    cf09_max14830_backend_fake_bus_t *fake,
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    const uint8_t *payload,
    size_t payload_len)
{
    cf09_a_backend_ops_t ops = cf09_max14830_backend_core_ops(backend);

    return ops.start_tx(ops.user, token, payload, payload_len) ==
               CF09_A_BACKEND_STARTED &&
           cf09_max14830_backend_fake_complete_async(
               fake, CF09_A_COMMIT_COMMITTED) &&
           cf09_max14830_backend_test_record_host_result(
               backend, token, CF09_A_COMMIT_COMMITTED) ==
               CF09_MAX14830_BACKEND_OK &&
           ops.close_host_tx(ops.user, token);
}

static bool fire_guard(
    cf09_max14830_backend_fake_bus_t *fake,
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t *source_out)
{
    size_t index = (size_t)token->physical_channel;
    cf09_a_attempt_token_t fired_token;

    if (index >= (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS ||
        cf09_max14830_fake_finish_final_character(
            &fake->device, (uint8_t)index) !=
            CF09_MAX14830_RESULT_OK ||
        !cf09_max14830_backend_fake_fire_guard(
            fake, (uint8_t)index, &fired_token) ||
        !cf09_a_token_equal(&fired_token, token)) {
        return false;
    }
    return cf09_max14830_backend_on_guard_elapsed(
               backend, &fired_token, source_out) ==
           CF09_MAX14830_BACKEND_OK;
}

static bool test_init_is_safe_and_probe_trace_is_exact(void)
{
    static const uint8_t expected_status_commands[] = {
        0x02U, 0x11U, 0x22U, 0x31U,
        0x42U, 0x51U, 0x62U, 0x71U
    };
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_max14830_backend_validation_t validation;
    size_t index;

    CHECK(init_backend(&fake, &backend));
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.initialized);
    CHECK(!validation.qualified_b4);
    CHECK(!validation.outputs_enabled);
    CHECK(validation.reset_active);
    CHECK(fake.reset_asserted);
    CHECK(!fake.output_gate_enabled);

    CHECK(cf09_max14830_backend_reset_and_probe(&backend) ==
          CF09_MAX14830_BACKEND_OK);
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.qualified_b4);
    CHECK(validation.revision_value == 0xB4U);
    CHECK(!validation.reset_active);
    CHECK(!validation.outputs_enabled);
    CHECK(fake.control_trace_count == 11U);
    CHECK(fake.control_trace[0U].len == 2U);
    CHECK(fake.control_trace[0U].bytes[0U] == 0x9FU);
    CHECK(fake.control_trace[0U].bytes[1U] == 0xCEU);
    CHECK(fake.control_trace[1U].bytes[0U] == 0x05U);
    CHECK(fake.control_trace[1U].bytes[1U] == 0x00U);
    CHECK(fake.control_trace[2U].bytes[0U] == 0x9FU);
    CHECK(fake.control_trace[2U].bytes[1U] == 0xCDU);
    for (index = 0U;
         index < sizeof(expected_status_commands) /
                     sizeof(expected_status_commands[0]);
         ++index) {
        CHECK(fake.control_trace[3U + index].len == 2U);
        CHECK(fake.control_trace[3U + index].bytes[0U] ==
              expected_status_commands[index]);
        CHECK(fake.control_trace[3U + index].bytes[1U] == 0U);
    }
    CHECK(strcmp(validation.model_status,
                 "MODEL_ONLY / HOST_NATIVE_TEST_PASS / "
                 "ISR_PATCH_NATIVE_PASS") == 0);
    CHECK(strcmp(validation.hardware_status,
                 "NOT_RUN / ESP_IDF_AND_HARDWARE_REQUIRED") == 0);
    CHECK(strcmp(validation.uart3_irq_policy,
                 CF09_MAX14830_BACKEND_UART3_IRQ_POLICY) == 0);
    return true;
}

static bool test_legacy_revision_is_blocked(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_backend(&fake, &backend));
    cf09_max14830_backend_fake_set_revision(&fake, 0xB3U);
    CHECK(cf09_max14830_backend_reset_and_probe(&backend) ==
          CF09_MAX14830_BACKEND_UNSUPPORTED_SILICON);
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(!validation.qualified_b4);
    CHECK(validation.revision_value == 0xB3U);
    CHECK(validation.revision_class ==
          CF09_MAX14830_REVISION_LEGACY_TXFIFO_ERRATUM);
    CHECK(validation.reset_active);
    CHECK(!fake.output_gate_enabled);
    return true;
}

static bool test_read_failure_still_attempts_extended_cleanup(void)
{
    const cf09_max14830_backend_io_result_t script[] = {
        CF09_MAX14830_BACKEND_IO_OK,
        CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE,
        CF09_MAX14830_BACKEND_IO_OK
    };
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_backend(&fake, &backend));
    CHECK(cf09_max14830_backend_fake_script_control(
        &fake, script, sizeof(script) / sizeof(script[0])));
    CHECK(cf09_max14830_backend_reset_and_probe(&backend) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(fake.control_trace_count == 3U);
    CHECK(fake.control_trace[2U].bytes[0U] == 0x9FU);
    CHECK(fake.control_trace[2U].bytes[1U] == 0xCDU);
    CHECK(!fake.device.extended_registers_enabled);
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.faulted);
    CHECK(!validation.extended_state_unknown);
    CHECK(!validation.qualified_b4);
    CHECK(validation.reset_active);
    return true;
}

static bool test_unknown_cleanup_quarantines_and_requires_reset(void)
{
    const cf09_max14830_backend_io_result_t script[] = {
        CF09_MAX14830_BACKEND_IO_OK,
        CF09_MAX14830_BACKEND_IO_OK,
        CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE
    };
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_backend(&fake, &backend));
    CHECK(cf09_max14830_backend_fake_script_control(
        &fake, script, sizeof(script) / sizeof(script[0])));
    CHECK(cf09_max14830_backend_reset_and_probe(&backend) ==
          CF09_MAX14830_BACKEND_IO_UNKNOWN);
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.faulted);
    CHECK(validation.extended_state_unknown);
    CHECK(validation.reset_active);
    CHECK(!validation.qualified_b4);
    CHECK(validation.completion_health ==
          CF09_A_COMPLETION_PATH_UNKNOWN);
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, false) ==
          CF09_MAX14830_BACKEND_OK);
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(!validation.faulted);
    CHECK(!validation.extended_state_unknown);
    CHECK(!validation.qualified_b4);
    CHECK(validation.reset_active);
    return true;
}

static bool test_output_enable_requires_all_evidence_and_health(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    uint8_t logical;

    CHECK(init_and_probe(&fake, &backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    CHECK(!ops.set_outputs_enabled(ops.user, true));
    for (logical = 2U; logical <= 3U; ++logical) {
        size_t index = (size_t)(logical - 2U);
        CHECK(cf09_max14830_backend_attest_channel_configured(
                  &backend, logical,
                  backend._resource_generation[index]) ==
              CF09_MAX14830_BACKEND_OK);
        CHECK(cf09_max14830_backend_verify_channel_idle(
                  &backend, logical,
                  backend._resource_generation[index]) ==
              CF09_MAX14830_BACKEND_OK);
    }
    CHECK(!ops.set_outputs_enabled(ops.user, true));
    CHECK(cf09_max14830_backend_attest_channel_configured(
              &backend, 4U, backend._resource_generation[2U]) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_backend_verify_channel_idle(
              &backend, 4U, backend._resource_generation[2U]) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(!ops.set_outputs_enabled(ops.user, true));
    CHECK(cf09_max14830_backend_attest_uart3_irq_masked(
              &backend, backend._device_generation) ==
          CF09_MAX14830_BACKEND_OK);
    fake.health = CF09_A_COMPLETION_PATH_IRQ_STUCK;
    CHECK(!ops.set_outputs_enabled(ops.user, true));
    fake.health = CF09_A_COMPLETION_PATH_HEALTHY;
    CHECK(ops.set_outputs_enabled(ops.user, true));
    CHECK(fake.output_gate_enabled);
    CHECK(ops.set_outputs_enabled(ops.user, false));
    CHECK(!fake.output_gate_enabled);
    return true;
}

static bool test_start_validates_bounds_fingerprint_and_copies_payload(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    uint8_t payload[] = {0x11U, 0x22U, 0x33U};
    uint8_t oversized[CF09_MAX14830_FIFO_CAPACITY_WORDS + 1U] = {0U};
    cf09_a_attempt_token_t token;
    uint64_t starts;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    starts = fake.start_count;
    token.payload_fingerprint++;
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_REJECTED);
    CHECK(fake.start_count == starts);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token, payload, 0U) ==
          CF09_A_BACKEND_REJECTED);
    CHECK(fake.start_count == starts);
    token = make_token(&backend, 2U, 1U, 1U,
                       oversized, sizeof(oversized));
    CHECK(ops.start_tx(ops.user, &token,
                       oversized, sizeof(oversized)) ==
          CF09_A_BACKEND_REJECTED);
    CHECK(fake.start_count == starts);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    token.physical_channel = 2U;
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_REJECTED);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    token.chip_select_id++;
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_REJECTED);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    token.bus_generation++;
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_REJECTED);
    CHECK(fake.start_count == starts);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_STARTED);
    CHECK(fake.start_count == starts + 1U);
    payload[0U] = 0xEEU;
    CHECK(backend._host.payload[0U] == 0x11U);
    CHECK(fake.pending_async_frame.bytes[1U] == 0x11U);
    CHECK(cf09_a_token_equal(&backend._host.token, &token));
    return true;
}

static bool test_committed_host_token_moves_to_serializer_slot(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x51U, 0x52U};
    cf09_a_attempt_token_t token;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 3U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.host_state == CF09_MAX14830_BACKEND_HOST_EMPTY);
    CHECK(validation.active_channels == 1U);
    CHECK(cf09_max14830_backend_channel_state(&backend, 3U) ==
          CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING);
    CHECK(cf09_a_token_equal(&backend._channels[1U].token, &token));
    return true;
}

static bool test_not_committed_host_result_retains_no_channel(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0xA1U};
    cf09_a_attempt_token_t token;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_STARTED);
    CHECK(cf09_max14830_backend_fake_complete_async(
        &fake, CF09_A_COMMIT_NOT_COMMITTED));
    CHECK(cf09_max14830_backend_test_record_host_result(
              &backend, &token, CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(ops.close_host_tx(ops.user, &token));
    CHECK(backend._host.state == CF09_MAX14830_BACKEND_HOST_EMPTY);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_EMPTY);
    CHECK((backend._idle_mask & 0x01U) != 0U);
    token = make_token(&backend, 2U, 2U, 2U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_STARTED);
    CHECK(cf09_max14830_backend_fake_complete_async(
        &fake, CF09_A_COMMIT_NOT_COMMITTED));
    CHECK(cf09_max14830_backend_test_record_host_result(
              &backend, &token, CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(ops.close_host_tx(ops.user, &token));
    return true;
}

static bool test_three_serializers_can_be_active_but_same_channel_is_busy(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x70U};
    cf09_a_attempt_token_t tokens[3];
    size_t index;
    uint64_t starts;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    for (index = 0U; index < 3U; ++index) {
        tokens[index] = make_token(
            &backend, (uint8_t)(2U + index),
            (uint32_t)(index + 1U), (uint64_t)(index + 1U),
            payload, sizeof(payload));
        CHECK(start_and_commit(&fake, &backend, &tokens[index],
                               payload, sizeof(payload)));
    }
    CHECK(cf09_max14830_backend_validate(&backend).active_channels == 3U);
    starts = fake.start_count;
    tokens[0U].frame.sequence++;
    tokens[0U].transfer_no++;
    CHECK(ops.start_tx(ops.user, &tokens[0U],
                       payload, sizeof(payload)) ==
          CF09_A_BACKEND_BUSY);
    CHECK(fake.start_count == starts);
    return true;
}

static bool test_isr5_requires_guard_before_completion(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x90U, 0x91U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t token2;
    cf09_a_attempt_token_t observed;
    cf09_a_completion_source_t source = CF09_A_COMPLETION_IRQ;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 4U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 4U, CF09_A_COMPLETION_STATUS_POLL,
              &observed) == CF09_MAX14830_BACKEND_NOT_READY);
    CHECK(cf09_max14830_backend_channel_state(&backend, 4U) ==
          CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING);
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 2U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 4U, CF09_A_COMPLETION_STATUS_POLL,
              &observed) == CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_a_token_equal(&observed, &token));
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.tail_count == 1U);
    CHECK(validation.completion_count == 0U);
    CHECK(cf09_max14830_backend_channel_state(&backend, 4U) ==
          CF09_MAX14830_BACKEND_CHANNEL_TAIL_PENDING);
    CHECK(fire_guard(&fake, &backend, &token, &source));
    CHECK(source == CF09_A_COMPLETION_STATUS_POLL);
    CHECK(cf09_max14830_backend_channel_state(&backend, 4U) ==
          CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY);
    CHECK(cf09_max14830_backend_on_guard_elapsed(
              &backend, &token, &source) ==
          CF09_MAX14830_BACKEND_STALE_TOKEN);
    CHECK(cf09_max14830_backend_test_release_completion(
              &backend, &token) == CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_backend_channel_state(&backend, 4U) ==
          CF09_MAX14830_BACKEND_CHANNEL_EMPTY);
    CHECK((backend._idle_mask & 0x04U) != 0U);
    token2 = make_token(&backend, 4U, 2U, 2U,
                        payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token2, payload, sizeof(payload)) ==
          CF09_A_BACKEND_STARTED);
    CHECK(cf09_max14830_backend_fake_complete_async(
        &fake, CF09_A_COMMIT_COMMITTED));
    CHECK(cf09_max14830_backend_test_record_host_result(
              &backend, &token2, CF09_A_COMMIT_COMMITTED) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(ops.close_host_tx(ops.user, &token2));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 2U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 4U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(fire_guard(&fake, &backend, &token2, &source));
    CHECK(cf09_max14830_backend_test_release_completion(
              &backend, &token2) == CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_backend_validate(&backend).
              rejected_event_count == 1U);
    return true;
}

static bool test_reset_invalidates_generations_and_late_events(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x10U};
    cf09_a_attempt_token_t token;
    cf09_a_completion_source_t source;
    uint32_t bus_before;
    uint32_t device_before;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_get_generations(
              &backend, &bus_before, &device_before) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, true) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(backend._bus_generation == bus_before + 1U);
    CHECK(backend._device_generation == device_before + 1U);
    CHECK(!backend._qualified_b4);
    CHECK(backend._configured_mask == 0U);
    CHECK(backend._idle_mask == 0U);
    CHECK(!backend._uart3_irq_masked);
    CHECK(cf09_max14830_backend_on_guard_elapsed(
              &backend, &token, &source) ==
          CF09_MAX14830_BACKEND_STALE_TOKEN);
    CHECK(ops.start_tx(ops.user, &token,
                       payload, sizeof(payload)) ==
          CF09_A_BACKEND_REJECTED);
    return true;
}

static bool test_timeout_cancels_guard_and_faults_closed(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x33U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 3U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 1U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 3U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(fake.guard_armed[1U]);
    CHECK(cf09_max14830_backend_mark_timeout(&backend, &token) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(!fake.guard_armed[1U]);
    CHECK(!fake.output_gate_enabled);
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.faulted);
    CHECK(validation.uncertain_channels == 1U);
    CHECK(validation.completion_health ==
          CF09_A_COMPLETION_PATH_UNKNOWN);
    return true;
}

static bool test_unknown_acceptance_is_immediately_fail_closed(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x44U};
    cf09_a_attempt_token_t token;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    fake.start_result = CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE;
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token,
                       payload, sizeof(payload)) ==
          CF09_A_BACKEND_UNKNOWN_ACCEPTANCE);
    CHECK(backend._host.state ==
          CF09_MAX14830_BACKEND_HOST_UNCERTAIN);
    CHECK(backend._faulted);
    CHECK(!backend._outputs_enabled);
    CHECK(!fake.output_gate_enabled);
    CHECK(ops.abort_host_tx(ops.user, &token));
    CHECK(!fake.async_pending);
    CHECK(backend._host.state == CF09_MAX14830_BACKEND_HOST_EMPTY);
    return true;
}

static bool test_deferred_unknown_and_abort_failure_stay_quarantined(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_fake_bus_t abort_fake;
    cf09_max14830_backend_t backend;
    cf09_max14830_backend_t abort_backend;
    cf09_a_backend_ops_t ops;
    cf09_a_backend_ops_t abort_ops;
    const uint8_t payload[] = {0x45U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t abort_token;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token, payload, sizeof(payload)) ==
          CF09_A_BACKEND_STARTED);
    CHECK(cf09_max14830_backend_fake_complete_async(
        &fake, CF09_A_COMMIT_UNKNOWN));
    CHECK(cf09_max14830_backend_test_record_host_result(
              &backend, &token, CF09_A_COMMIT_UNKNOWN) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(ops.close_host_tx(ops.user, &token));
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    CHECK(backend._faulted);
    CHECK(!backend._outputs_enabled);

    CHECK(init_and_probe(&abort_fake, &abort_backend));
    CHECK(attest_all_and_enable(&abort_backend));
    abort_fake.start_result =
        CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE;
    abort_fake.abort_ok = false;
    abort_ops = cf09_max14830_backend_core_ops(&abort_backend);
    abort_token = make_token(&abort_backend, 2U, 1U, 1U,
                             payload, sizeof(payload));
    CHECK(abort_ops.start_tx(
              abort_ops.user,
              &abort_token,
              payload,
              sizeof(payload)) == CF09_A_BACKEND_UNKNOWN_ACCEPTANCE);
    CHECK(!abort_ops.abort_host_tx(abort_ops.user, &abort_token));
    CHECK(abort_backend._host.state ==
          CF09_MAX14830_BACKEND_HOST_UNCERTAIN);
    CHECK(abort_fake.async_pending);
    CHECK(abort_backend._faulted);
    CHECK(!abort_backend._outputs_enabled);
    return true;
}

static bool test_null_abort_does_not_poison_mutation_guard(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;

    CHECK(init_and_probe(&fake, &backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    CHECK(!ops.abort_host_tx(ops.user, NULL));
    CHECK(!backend._mutating);
    CHECK(ops.set_outputs_enabled(ops.user, false));
    return true;
}

static bool test_hard_start_failure_disables_outputs(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x45U};
    cf09_a_attempt_token_t token;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    fake.start_result = CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE;
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token,
                       payload, sizeof(payload)) ==
          CF09_A_BACKEND_FAILURE);
    CHECK(backend._faulted);
    CHECK(!backend._outputs_enabled);
    CHECK(!fake.output_gate_enabled);
    CHECK(backend._host.state == CF09_MAX14830_BACKEND_HOST_EMPTY);
    return true;
}

static bool test_quiesce_disables_and_detaches_sources(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    cf09_max14830_backend_validation_t validation;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    CHECK(ops.quiesce(ops.user));
    validation = cf09_max14830_backend_validate(&backend);
    CHECK(validation.quiesced);
    CHECK(validation.reset_active);
    CHECK(!validation.outputs_enabled);
    CHECK(!validation.qualified_b4);
    CHECK(fake.quiesced);
    CHECK(!fake.output_gate_enabled);
    CHECK(validation.completion_health ==
          CF09_A_COMPLETION_PATH_UNKNOWN);
    CHECK(!ops.set_outputs_enabled(ops.user, true));
    return true;
}

static bool test_counter_exhaustion_is_fail_closed(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x46U};
    cf09_a_attempt_token_t token;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    backend._started_count = UINT64_MAX;
    ops = cf09_max14830_backend_core_ops(&backend);
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(ops.start_tx(ops.user, &token,
                       payload, sizeof(payload)) ==
          CF09_A_BACKEND_FAILURE);
    CHECK(backend._started_count == UINT64_MAX);
    CHECK(backend._faulted);
    CHECK(!fake.output_gate_enabled);
    CHECK(fake.start_count == 0U);
    return true;
}

static bool test_generation_overflow_still_performs_physical_cleanup(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x47U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 0U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(fake.guard_armed[0U]);
    backend._device_generation = UINT32_MAX;
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, false) ==
          CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED);
    CHECK(!fake.guard_armed[0U]);
    CHECK(!backend._outputs_enabled);
    CHECK(backend._reset_active);
    CHECK(backend._faulted);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    return true;
}

static bool test_completion_counter_overflow_consumes_fired_guard(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x48U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;
    cf09_a_attempt_token_t fired;
    cf09_a_completion_source_t source;
    uint64_t cancels_before;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 0U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_fake_finish_final_character(
              &fake.device, 0U) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_backend_fake_fire_guard(
        &fake, 0U, &fired));
    backend._completion_count = UINT64_MAX;
    CHECK(cf09_max14830_backend_on_guard_elapsed(
              &backend, &fired, &source) ==
          CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED);
    CHECK(!backend._channels[0U].guard_armed);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    cancels_before = fake.guard_cancel_count;
    CHECK(cf09_max14830_backend_test_notify_reset(
              &backend, false) == CF09_MAX14830_BACKEND_OK);
    CHECK(fake.guard_cancel_count == cancels_before);
    return true;
}

static bool test_gate_failure_still_attempts_safe_reset(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_fake_bus_t probe_fake;
    cf09_max14830_backend_t backend = CF09_MAX14830_BACKEND_INITIALIZER;
    cf09_max14830_backend_t probe_backend;
    cf09_max14830_backend_platform_ops_t platform;
    uint64_t reset_before;

    cf09_max14830_backend_fake_bus_init(&fake);
    fake.gate_ok = false;
    platform = cf09_max14830_backend_fake_bus_ops(&fake);
    CHECK(cf09_max14830_backend_init(&backend, &platform) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(fake.reset_count == 1U);
    CHECK(fake.reset_asserted);
    CHECK(!backend._initialized);

    CHECK(init_backend(&probe_fake, &probe_backend));
    reset_before = probe_fake.reset_count;
    probe_fake.gate_ok = false;
    CHECK(cf09_max14830_backend_reset_and_probe(&probe_backend) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(probe_fake.reset_count == reset_before + 1U);
    CHECK(probe_fake.reset_asserted);
    CHECK(probe_backend._reset_active);
    CHECK(probe_backend._faulted);
    return true;
}

static bool test_failed_idle_recheck_clears_stale_evidence(void)
{
    const cf09_max14830_backend_io_result_t failure[] = {
        CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE
    };
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    uint32_t generation;

    CHECK(init_and_probe(&fake, &backend));
    generation = backend._resource_generation[0U];
    CHECK(cf09_max14830_backend_attest_channel_configured(
              &backend, 2U, generation) == CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_backend_verify_channel_idle(
              &backend, 2U, generation) == CF09_MAX14830_BACKEND_OK);
    CHECK((backend._idle_mask & 0x01U) != 0U);
    CHECK(cf09_max14830_backend_fake_script_control(
        &fake, failure, sizeof(failure) / sizeof(failure[0])));
    CHECK(cf09_max14830_backend_verify_channel_idle(
              &backend, 2U, generation) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK((backend._idle_mask & 0x01U) == 0U);
    return true;
}

static bool test_reprobe_requires_generation_advance(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    uint64_t reset_before;
    size_t trace_before;

    CHECK(init_and_probe(&fake, &backend));
    reset_before = fake.reset_count;
    trace_before = fake.control_trace_count;
    CHECK(cf09_max14830_backend_reset_and_probe(&backend) ==
          CF09_MAX14830_BACKEND_INVALID_STATE);
    CHECK(fake.reset_count == reset_before);
    CHECK(fake.control_trace_count == trace_before);
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, false) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(backend._device_generation == 1U);
    CHECK(cf09_max14830_backend_reset_and_probe(&backend) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(backend._qualified_b4);
    CHECK(backend._last_probe_device_generation == 1U);
    return true;
}

static bool test_channel_abort_quarantines_every_serializer(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x61U};
    cf09_a_attempt_token_t token0;
    cf09_a_attempt_token_t token1;
    cf09_a_attempt_token_t observed;
    cf09_a_completion_source_t source;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    token0 = make_token(&backend, 2U, 1U, 1U,
                        payload, sizeof(payload));
    token1 = make_token(&backend, 3U, 2U, 2U,
                        payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token0,
                           payload, sizeof(payload)));
    CHECK(start_and_commit(&fake, &backend, &token1,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 0U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(fake.guard_armed[0U]);
    CHECK(ops.abort_host_tx(ops.user, &token0));
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    CHECK(cf09_max14830_backend_channel_state(&backend, 3U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    CHECK(!fake.guard_armed[0U]);
    CHECK(backend._reset_active);
    CHECK(backend._faulted);
    CHECK(!backend._qualified_b4);
    CHECK(!backend._outputs_enabled);
    CHECK(cf09_max14830_backend_on_guard_elapsed(
              &backend, &token0, &source) ==
          CF09_MAX14830_BACKEND_NOT_READY);
    CHECK(backend._completion_count == 0U);
    return true;
}

static bool test_partial_reset_failure_rejects_late_guard(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x71U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;
    cf09_a_attempt_token_t fired;
    cf09_a_completion_source_t source;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 0U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_STATUS_POLL,
              &observed) == CF09_MAX14830_BACKEND_OK);
    fake.guard_cancel_ok = false;
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, false) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(fake.guard_armed[0U]);
    CHECK(backend._faulted);
    CHECK(backend._reset_active);
    CHECK(cf09_max14830_backend_fake_fire_guard(
        &fake, 0U, &fired));
    CHECK(cf09_a_token_equal(&fired, &token));
    CHECK(cf09_max14830_backend_on_guard_elapsed(
              &backend, &fired, &source) ==
          CF09_MAX14830_BACKEND_NOT_READY);
    CHECK(backend._completion_count == 0U);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    return true;
}

static bool test_timeout_cancel_failure_preserves_guard_for_reset(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x72U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    CHECK(cf09_max14830_backend_fake_raise_tail(&fake, 0U));
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_OK);
    fake.guard_cancel_ok = false;
    CHECK(cf09_max14830_backend_mark_timeout(&backend, &token) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(fake.guard_armed[0U]);
    CHECK(backend._channels[0U].guard_armed);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    fake.guard_cancel_ok = true;
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, false) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(!fake.guard_armed[0U]);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_EMPTY);
    return true;
}

static bool test_unowned_isr_bits_fail_closed(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x81U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    fake.device.registers[0U][CF09_MAX14830_REG_ISR] = 0x21U;
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(backend._channels[0U].last_isr == 0x21U);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    CHECK(backend._faulted);
    CHECK(!backend._outputs_enabled);
    return true;
}

static bool run_rx_empty_contamination_case(uint8_t isr_value)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    const uint8_t payload[] = {0x82U};
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t observed;

    CHECK(init_and_probe(&fake, &backend));
    CHECK(attest_all_and_enable(&backend));
    token = make_token(&backend, 2U, 1U, 1U,
                       payload, sizeof(payload));
    CHECK(start_and_commit(&fake, &backend, &token,
                           payload, sizeof(payload)));
    fake.device.registers[0U][CF09_MAX14830_REG_ISR] = isr_value;
    CHECK(cf09_max14830_backend_observe_tail(
              &backend, 2U, CF09_A_COMPLETION_IRQ, &observed) ==
          CF09_MAX14830_BACKEND_IO_FAILURE);
    CHECK(!fake.guard_armed[0U]);
    CHECK(!backend._channels[0U].guard_armed);
    CHECK(backend._channels[0U].last_isr == isr_value);
    CHECK(cf09_max14830_backend_channel_state(&backend, 2U) ==
          CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN);
    CHECK(backend._faulted);
    CHECK(!backend._outputs_enabled);
    CHECK(!fake.output_gate_enabled);
    return true;
}

static bool test_rx_empty_is_never_tx_path_evidence(void)
{
    CHECK((uint8_t)CF09_MAX14830_BACKEND_ISR_TX_EMPTY_BIT ==
          (uint8_t)CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT);
    CHECK((uint8_t)CF09_MAX14830_BACKEND_ISR_ALLOWED_TX_MASK == 0x20U);
    CHECK(run_rx_empty_contamination_case(0x40U));
    CHECK(run_rx_empty_contamination_case(0x60U));
    return true;
}

static bool test_reset_dispatch_enforces_generation_order(void)
{
    const cf09_a_session_id_t session = {1U, 2U};
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_fake_bus_t wrong_fake;
    cf09_max14830_backend_fake_bus_t recovery_fake;
    cf09_max14830_backend_t backend;
    cf09_max14830_backend_t wrong_backend;
    cf09_max14830_backend_t recovery_backend;
    cf09_a_backend_ops_t ops;
    cf09_a_backend_ops_t wrong_ops;
    cf09_a_backend_ops_t recovery_ops;
    cf09_a_driver_t driver = CF09_A_DRIVER_INITIALIZER;
    cf09_a_driver_t wrong_driver = CF09_A_DRIVER_INITIALIZER;
    cf09_a_driver_t recovery_driver = CF09_A_DRIVER_INITIALIZER;
    size_t index;

    CHECK(init_and_probe(&fake, &backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    CHECK(cf09_a_driver_init(&driver, session, &ops) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_max14830_backend_dispatch_reset(
              &backend, &driver, true) == CF09_A_RESULT_OK);
    CHECK(driver._bus_generation == backend._bus_generation);
    CHECK(driver._device_generation == backend._device_generation);
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        CHECK(driver._resource_generation[index] ==
              backend._resource_generation[index]);
    }

    CHECK(init_and_probe(&wrong_fake, &wrong_backend));
    wrong_ops = cf09_max14830_backend_core_ops(&wrong_backend);
    CHECK(cf09_a_driver_init(&wrong_driver, session, &wrong_ops) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_max14830_backend_test_notify_reset(
              &wrong_backend, false) == CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_max14830_backend_dispatch_reset(
              &wrong_backend, &wrong_driver, false) ==
          CF09_A_RESULT_INVALID_STATE);
    CHECK(wrong_driver._device_generation == 0U);
    CHECK(wrong_backend._device_generation == 1U);

    CHECK(init_and_probe(&recovery_fake, &recovery_backend));
    recovery_ops = cf09_max14830_backend_core_ops(&recovery_backend);
    CHECK(cf09_a_driver_init(
              &recovery_driver, session, &recovery_ops) ==
          CF09_A_RESULT_OK);
    recovery_fake.reset_ok = false;
    CHECK(cf09_max14830_backend_dispatch_reset(
              &recovery_backend, &recovery_driver, false) ==
          CF09_A_RESULT_BACKEND_FAILURE);
    CHECK(recovery_backend._reset_catchup_pending);
    CHECK(recovery_driver._device_generation == 1U);
    CHECK(recovery_backend._device_generation == 0U);
    recovery_fake.reset_ok = true;
    CHECK(cf09_max14830_backend_retry_reset_catchup(
              &recovery_backend, &recovery_driver) ==
          CF09_A_RESULT_OK);
    CHECK(!recovery_backend._reset_catchup_pending);
    CHECK(recovery_backend._device_generation ==
          recovery_driver._device_generation);
    return true;
}

static bool test_invalid_host_result_for_active_token_fails_closed(void)
{
    const cf09_a_session_id_t session = {3U, 4U};
    const uint8_t payload[] = {0x91U};
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    cf09_a_driver_t driver = CF09_A_DRIVER_INITIALIZER;
    cf09_a_tx_request_t request;
    cf09_a_attempt_token_t token;
    size_t index;

    CHECK(init_and_probe(&fake, &backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    CHECK(cf09_a_driver_init(&driver, session, &ops) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_bridge_up(&driver) == CF09_A_RESULT_OK);
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        uint8_t logical = (uint8_t)(index + 2U);
        uint32_t generation = backend._resource_generation[index];
        CHECK(cf09_max14830_backend_attest_channel_configured(
                  &backend, logical, generation) ==
              CF09_MAX14830_BACKEND_OK);
        CHECK(cf09_max14830_backend_verify_channel_idle(
                  &backend, logical, generation) ==
              CF09_MAX14830_BACKEND_OK);
        CHECK(cf09_a_mark_resource_configured(
                  &driver, (uint8_t)index, generation) ==
              CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_idle(
                  &driver, (uint8_t)index, generation) ==
              CF09_A_RESULT_OK);
    }
    CHECK(cf09_max14830_backend_attest_uart3_irq_masked(
              &backend, backend._device_generation) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_OK);
    memset(&request, 0, sizeof(request));
    request.frame.epoch = 1U;
    request.frame.sequence = 1U;
    request.logical_channel = 2U;
    request.payload = payload;
    request.payload_len = sizeof(payload);
    CHECK(cf09_a_begin_tx(&driver, &request, &token) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_max14830_backend_fake_complete_async(
        &fake, CF09_A_COMMIT_COMMITTED));
    CHECK(cf09_max14830_backend_dispatch_host_result(
              &backend,
              &driver,
              &token,
              (cf09_a_commit_result_t)99) ==
          CF09_A_RESULT_INVALID_ARGUMENT);
    CHECK(backend._host.state ==
          CF09_MAX14830_BACKEND_HOST_UNCERTAIN);
    CHECK(backend._faulted);
    CHECK(backend._reset_active);
    CHECK(!backend._outputs_enabled);
    CHECK(!fake.output_gate_enabled);
    return true;
}

static bool test_generic_core_end_to_end_three_routes_model_pass(void)
{
    cf09_max14830_backend_fake_bus_t fake;
    cf09_max14830_backend_t backend;
    cf09_a_backend_ops_t ops;
    cf09_a_driver_t driver = CF09_A_DRIVER_INITIALIZER;
    const cf09_a_session_id_t session = {
        UINT64_C(0xAA55AA55AA55AA55),
        UINT64_C(0x55AA55AA55AA55AA)
    };
    const uint8_t payload[] = {0xC0U, 0xDEU};
    uint8_t logical;

    CHECK(init_and_probe(&fake, &backend));
    ops = cf09_max14830_backend_core_ops(&backend);
    CHECK(cf09_a_driver_init(&driver, session, &ops) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_bridge_up(&driver) == CF09_A_RESULT_OK);
    for (logical = 2U; logical <= 4U; ++logical) {
        size_t index = (size_t)(logical - 2U);
        uint32_t generation = backend._resource_generation[index];
        CHECK(cf09_max14830_backend_attest_channel_configured(
                  &backend, logical, generation) ==
              CF09_MAX14830_BACKEND_OK);
        CHECK(cf09_max14830_backend_verify_channel_idle(
                  &backend, logical, generation) ==
              CF09_MAX14830_BACKEND_OK);
        CHECK(cf09_a_mark_resource_configured(
                  &driver, (uint8_t)index, generation) ==
              CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_idle(
                  &driver, (uint8_t)index, generation) ==
              CF09_A_RESULT_OK);
    }
    CHECK(cf09_max14830_backend_attest_uart3_irq_masked(
              &backend, backend._device_generation) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_OK);

    for (logical = 2U; logical <= 4U; ++logical) {
        cf09_a_tx_request_t request;
        cf09_a_attempt_token_t token;
        cf09_a_attempt_token_t observed;
        cf09_a_completion_source_t source;
        size_t index = (size_t)(logical - 2U);

        memset(&request, 0, sizeof(request));
        request.frame.epoch = 1U;
        request.frame.sequence = (uint32_t)(logical - 1U);
        request.logical_channel = logical;
        request.payload = payload;
        request.payload_len = sizeof(payload);
        CHECK(cf09_a_begin_tx(&driver, &request, &token) ==
              CF09_A_RESULT_OK);
        CHECK(cf09_max14830_backend_fake_complete_async(
            &fake, CF09_A_COMMIT_COMMITTED));
        if (logical == 2U) {
            cf09_a_driver_t wrong = CF09_A_DRIVER_INITIALIZER;
            CHECK(cf09_max14830_backend_dispatch_host_result(
                      &backend,
                      &wrong,
                      &token,
                      CF09_A_COMMIT_COMMITTED) ==
                  CF09_A_RESULT_INVALID_ARGUMENT);
            driver._mutating = true;
            CHECK(cf09_max14830_backend_dispatch_host_result(
                      &backend,
                      &driver,
                      &token,
                      CF09_A_COMMIT_COMMITTED) ==
                  CF09_A_RESULT_REENTRANT);
            driver._mutating = false;
            CHECK(backend._host.state ==
                  CF09_MAX14830_BACKEND_HOST_IN_FLIGHT);
        }
        CHECK(cf09_max14830_backend_dispatch_host_result(
                  &backend,
                  &driver,
                  &token,
                  CF09_A_COMMIT_COMMITTED) ==
              CF09_A_RESULT_OK);
        CHECK(cf09_max14830_backend_fake_raise_tail(
            &fake, (uint8_t)index));
        CHECK(cf09_max14830_backend_observe_tail(
                  &backend, logical, CF09_A_COMPLETION_IRQ,
                  &observed) == CF09_MAX14830_BACKEND_OK);
        CHECK(cf09_a_token_equal(&observed, &token));
        CHECK(fire_guard(&fake, &backend, &token, &source));
        if (logical < 4U) {
            if (logical == 2U) {
                cf09_a_completion_source_t wrong_source =
                    source == CF09_A_COMPLETION_IRQ ?
                        CF09_A_COMPLETION_STATUS_POLL :
                        CF09_A_COMPLETION_IRQ;
                CHECK(cf09_max14830_backend_dispatch_completion(
                          &backend,
                          &driver,
                          &token,
                          wrong_source) ==
                      CF09_A_RESULT_INVALID_ARGUMENT);
                CHECK(driver._slots[index].state ==
                      CF09_A_ATTEMPT_HOST_COMMITTED);
                CHECK(backend._channels[index].state ==
                      CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY);
            }
            CHECK(cf09_max14830_backend_dispatch_completion(
                      &backend, &driver, &token, source) ==
                  CF09_A_RESULT_OK);
        } else {
            CHECK(cf09_a_on_serializer_done_latched(
                      &driver, &token) == CF09_A_RESULT_OK);
            CHECK(cf09_a_confirm_serializer_complete(
                      &driver, &token, source) == CF09_A_RESULT_OK);
            CHECK(cf09_a_validate(&driver).model_status !=
                  CF09_A_MODEL_PASS);
            CHECK(cf09_max14830_backend_validate(&backend).
                      completion_health ==
                  CF09_A_COMPLETION_PATH_UNKNOWN);
            CHECK(cf09_max14830_backend_test_release_completion(
                      &backend, &token) ==
                  CF09_MAX14830_BACKEND_OK);
        }
        CHECK(cf09_a_reap_complete(&driver, &token) ==
              CF09_A_RESULT_OK);
    }
    {
        cf09_a_validation_t validation = cf09_a_validate(&driver);
        CHECK(validation.model_status == CF09_A_MODEL_PASS);
        CHECK(validation.completed_count == 3U);
        CHECK(validation.completed_route_mask ==
              (uint8_t)CF09_A_EXTERNAL_TX_COMPLETE_MASK);
        CHECK(validation.open_attempts == 0U);
        CHECK(strcmp(validation.hardware_status,
                     "NOT_RUN / HARDWARE_REQUIRED") == 0);
        CHECK(strcmp(validation.connector_delivery_status,
                     "NOT_RUN / NOT_PROVEN") == 0);
    }
    CHECK(cf09_max14830_backend_test_notify_reset(&backend, false) ==
          CF09_MAX14830_BACKEND_OK);
    CHECK(cf09_a_validate(&driver).model_status != CF09_A_MODEL_PASS);
    return true;
}

static bool test_result_names_are_total(void)
{
    CHECK(strcmp(cf09_max14830_backend_result_name(
                     CF09_MAX14830_BACKEND_OK), "OK") == 0);
    CHECK(strcmp(cf09_max14830_backend_result_name(
                     CF09_MAX14830_BACKEND_IO_UNKNOWN),
                 "IO_UNKNOWN") == 0);
    CHECK(strcmp(cf09_max14830_backend_result_name(
                     (cf09_max14830_backend_result_t)99),
                 "INVALID_ENUM") == 0);
    return true;
}

int main(void)
{
    static const cf09_backend_test_case_t tests[] = {
        {"init_is_safe_and_probe_trace_is_exact",
         test_init_is_safe_and_probe_trace_is_exact},
        {"legacy_revision_is_blocked",
         test_legacy_revision_is_blocked},
        {"read_failure_still_attempts_extended_cleanup",
         test_read_failure_still_attempts_extended_cleanup},
        {"unknown_cleanup_quarantines_and_requires_reset",
         test_unknown_cleanup_quarantines_and_requires_reset},
        {"output_enable_requires_all_evidence_and_health",
         test_output_enable_requires_all_evidence_and_health},
        {"start_validates_bounds_fingerprint_and_copies_payload",
         test_start_validates_bounds_fingerprint_and_copies_payload},
        {"committed_host_token_moves_to_serializer_slot",
         test_committed_host_token_moves_to_serializer_slot},
        {"not_committed_host_result_retains_no_channel",
         test_not_committed_host_result_retains_no_channel},
        {"three_serializers_can_be_active_but_same_channel_is_busy",
         test_three_serializers_can_be_active_but_same_channel_is_busy},
        {"isr5_requires_guard_before_completion",
         test_isr5_requires_guard_before_completion},
        {"reset_invalidates_generations_and_late_events",
         test_reset_invalidates_generations_and_late_events},
        {"timeout_cancels_guard_and_faults_closed",
         test_timeout_cancels_guard_and_faults_closed},
        {"unknown_acceptance_is_immediately_fail_closed",
         test_unknown_acceptance_is_immediately_fail_closed},
        {"deferred_unknown_and_abort_failure_stay_quarantined",
         test_deferred_unknown_and_abort_failure_stay_quarantined},
        {"null_abort_does_not_poison_mutation_guard",
         test_null_abort_does_not_poison_mutation_guard},
        {"hard_start_failure_disables_outputs",
         test_hard_start_failure_disables_outputs},
        {"quiesce_disables_and_detaches_sources",
         test_quiesce_disables_and_detaches_sources},
        {"counter_exhaustion_is_fail_closed",
         test_counter_exhaustion_is_fail_closed},
        {"generation_overflow_still_performs_physical_cleanup",
         test_generation_overflow_still_performs_physical_cleanup},
        {"completion_counter_overflow_consumes_fired_guard",
         test_completion_counter_overflow_consumes_fired_guard},
        {"gate_failure_still_attempts_safe_reset",
         test_gate_failure_still_attempts_safe_reset},
        {"failed_idle_recheck_clears_stale_evidence",
         test_failed_idle_recheck_clears_stale_evidence},
        {"reprobe_requires_generation_advance",
         test_reprobe_requires_generation_advance},
        {"channel_abort_quarantines_every_serializer",
         test_channel_abort_quarantines_every_serializer},
        {"partial_reset_failure_rejects_late_guard",
         test_partial_reset_failure_rejects_late_guard},
        {"timeout_cancel_failure_preserves_guard_for_reset",
         test_timeout_cancel_failure_preserves_guard_for_reset},
        {"unowned_isr_bits_fail_closed",
         test_unowned_isr_bits_fail_closed},
        {"rx_empty_is_never_tx_path_evidence",
         test_rx_empty_is_never_tx_path_evidence},
        {"reset_dispatch_enforces_generation_order",
         test_reset_dispatch_enforces_generation_order},
        {"invalid_host_result_for_active_token_fails_closed",
         test_invalid_host_result_for_active_token_fails_closed},
        {"generic_core_end_to_end_three_routes_model_pass",
         test_generic_core_end_to_end_three_routes_model_pass},
        {"result_names_are_total", test_result_names_are_total}
    };
    size_t index;
    size_t passed = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            (void)printf("CF09 MAX14830 backend test failed: %s\n",
                         tests[index].name);
            return 1;
        }
        passed++;
    }
    (void)printf(
        "CF09 MAX14830 backend tests: %zu/%zu PASS "
        "(MODEL_ONLY; native compile PASS; hardware/electrical NOT_RUN)\n",
        passed,
        sizeof(tests) / sizeof(tests[0]));
    return 0;
}
