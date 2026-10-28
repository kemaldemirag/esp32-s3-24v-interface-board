#include "cf09_max14830_backend_v1.h"

#include <limits.h>
#include <string.h>

static bool cf09_max14830_backend_logical_to_index(
    uint8_t logical_channel,
    size_t *index_out)
{
    if (index_out == NULL ||
        logical_channel < (uint8_t)CF09_MAX14830_LOGICAL_TX_FIRST ||
        logical_channel > (uint8_t)CF09_MAX14830_LOGICAL_TX_LAST) {
        return false;
    }
    *index_out = (size_t)(logical_channel -
                          (uint8_t)CF09_MAX14830_LOGICAL_TX_FIRST);
    return true;
}

static cf09_max14830_backend_result_t cf09_max14830_backend_enter(
    cf09_max14830_backend_t *backend)
{
    if (backend == NULL || !backend->_initialized) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    if (backend->_mutating) {
        return CF09_MAX14830_BACKEND_BUSY;
    }
    backend->_mutating = true;
    return CF09_MAX14830_BACKEND_OK;
}

static void cf09_max14830_backend_leave(
    cf09_max14830_backend_t *backend)
{
    backend->_mutating = false;
}

static bool cf09_max14830_backend_platform_valid(
    const cf09_max14830_backend_platform_ops_t *platform)
{
    return platform != NULL && platform->control_transfer != NULL &&
           platform->start_transfer != NULL &&
           platform->abort_transfer != NULL &&
           platform->set_reset != NULL && platform->wait_ready != NULL &&
           platform->set_output_gate != NULL &&
           platform->arm_final_character_guard != NULL &&
           platform->cancel_final_character_guard != NULL &&
           platform->quiesce != NULL &&
           platform->completion_health != NULL;
}

static void cf09_max14830_backend_clear_attestations(
    cf09_max14830_backend_t *backend)
{
    backend->_qualified_b4 = false;
    backend->_revision_value = 0U;
    backend->_revision_class = CF09_MAX14830_REVISION_REJECTED;
    backend->_configured_mask = 0U;
    backend->_idle_mask = 0U;
    backend->_uart3_irq_masked = false;
    backend->_attested_bus_generation = 0U;
    backend->_attested_device_generation = 0U;
}

static bool cf09_max14830_backend_generations_attested(
    const cf09_max14830_backend_t *backend)
{
    return backend->_qualified_b4 &&
           backend->_attested_bus_generation == backend->_bus_generation &&
           backend->_attested_device_generation ==
               backend->_device_generation;
}

static void cf09_max14830_backend_latch_fault(
    cf09_max14830_backend_t *backend,
    bool extended_unknown)
{
    backend->_faulted = true;
    backend->_extended_state_unknown =
        backend->_extended_state_unknown || extended_unknown;
    backend->_outputs_enabled = false;
    (void)backend->_platform.set_output_gate(
        backend->_platform.user, false);
}

static bool cf09_max14830_backend_disable_and_assert_reset(
    cf09_max14830_backend_t *backend)
{
    bool gate_ok;
    bool reset_ok;

    gate_ok = backend->_platform.set_output_gate(
        backend->_platform.user, false);
    reset_ok = backend->_platform.set_reset(
        backend->_platform.user, true);
    backend->_outputs_enabled = false;
    backend->_reset_active = reset_ok;
    return gate_ok && reset_ok;
}

static bool cf09_max14830_backend_quarantine_channels(
    cf09_max14830_backend_t *backend)
{
    bool ok = true;
    size_t index;

    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        cf09_max14830_backend_channel_slot_t *channel =
            &backend->_channels[index];
        if (channel->guard_armed) {
            bool cancelled =
                backend->_platform.cancel_final_character_guard(
                    backend->_platform.user,
                    (uint8_t)index,
                    &channel->token);
            if (cancelled) {
                channel->guard_armed = false;
            }
            ok = cancelled && ok;
        }
        if (channel->state != CF09_MAX14830_BACKEND_CHANNEL_EMPTY) {
            channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        }
    }
    return ok;
}

static bool cf09_max14830_backend_token_route_valid(
    const cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    size_t *index_out)
{
    size_t index;

    if (token == NULL ||
        !cf09_max14830_backend_logical_to_index(
            token->logical_channel, &index)) {
        return false;
    }
    if (token->serializer_id != token->logical_channel ||
        token->resource_id != (uint8_t)index ||
        token->physical_channel != (uint8_t)index ||
        token->device_id != (uint8_t)CF09_A_DEVICE_ID ||
        token->chip_select_id != (uint8_t)CF09_A_CHIP_SELECT_ID ||
        token->resource_generation !=
            backend->_resource_generation[index] ||
        token->bus_generation != backend->_bus_generation ||
        token->device_generation != backend->_device_generation) {
        return false;
    }
    if (index_out != NULL) {
        *index_out = index;
    }
    return true;
}

static cf09_max14830_backend_result_t
cf09_max14830_backend_control_frame(
    cf09_max14830_backend_t *backend,
    const cf09_max14830_spi_frame_t *frame,
    uint8_t *rx_bytes,
    size_t rx_capacity,
    size_t *rx_len_out)
{
    cf09_max14830_backend_io_result_t io_result;
    size_t ignored_len = 0U;
    size_t *selected_len = rx_len_out != NULL ? rx_len_out : &ignored_len;

    *selected_len = 0U;
    io_result = backend->_platform.control_transfer(
        backend->_platform.user,
        frame->bytes,
        frame->len,
        rx_bytes,
        rx_capacity,
        selected_len);
    switch (io_result) {
    case CF09_MAX14830_BACKEND_IO_OK:
        return CF09_MAX14830_BACKEND_OK;
    case CF09_MAX14830_BACKEND_IO_BUSY:
        return CF09_MAX14830_BACKEND_BUSY;
    case CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE:
        return CF09_MAX14830_BACKEND_IO_UNKNOWN;
    case CF09_MAX14830_BACKEND_IO_REJECTED:
    case CF09_MAX14830_BACKEND_IO_NOT_COMMITTED:
    case CF09_MAX14830_BACKEND_IO_TRANSFER_FAILURE:
    default:
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
}

static cf09_max14830_backend_result_t
cf09_max14830_backend_read_register(
    cf09_max14830_backend_t *backend,
    uint8_t uart,
    uint8_t register_address,
    uint8_t *value_out)
{
    cf09_max14830_spi_frame_t frame;
    cf09_max14830_result_t codec_result;
    cf09_max14830_backend_result_t result;
    uint8_t rx[2] = {0U, 0U};
    size_t rx_len = 0U;

    if (value_out == NULL) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    codec_result = cf09_max14830_build_register_read(
        uart, register_address, &frame);
    if (codec_result != CF09_MAX14830_RESULT_OK) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    result = cf09_max14830_backend_control_frame(
        backend, &frame, rx, sizeof(rx), &rx_len);
    if (result != CF09_MAX14830_BACKEND_OK) {
        return result;
    }
    if (cf09_max14830_decode_single_read_data(
            rx, rx_len, value_out) != CF09_MAX14830_RESULT_OK) {
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    return CF09_MAX14830_BACKEND_OK;
}

static bool cf09_max14830_backend_has_open_channels(
    const cf09_max14830_backend_t *backend)
{
    size_t index;

    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY) {
            return true;
        }
    }
    return false;
}

static cf09_max14830_backend_channel_slot_t *
cf09_max14830_backend_find_channel_by_token(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    size_t *index_out)
{
    size_t index;

    if (!cf09_max14830_backend_token_route_valid(
            backend, token, &index)) {
        return NULL;
    }
    if (backend->_channels[index].state ==
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY ||
        !cf09_a_token_equal(&backend->_channels[index].token, token)) {
        return NULL;
    }
    if (index_out != NULL) {
        *index_out = index;
    }
    return &backend->_channels[index];
}

static void cf09_max14830_backend_dispatch_fail_closed(
    cf09_max14830_backend_t *backend)
{
    cf09_max14830_backend_result_t entered;
    bool reset_ok;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return;
    }
    (void)backend->_platform.set_output_gate(
        backend->_platform.user, false);
    backend->_outputs_enabled = false;
    if (backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY) {
        (void)backend->_platform.abort_transfer(
            backend->_platform.user);
        backend->_host.state = CF09_MAX14830_BACKEND_HOST_UNCERTAIN;
    }
    (void)cf09_max14830_backend_quarantine_channels(backend);
    reset_ok = backend->_platform.set_reset(
        backend->_platform.user, true);
    backend->_reset_active = reset_ok;
    cf09_max14830_backend_clear_attestations(backend);
    cf09_max14830_backend_latch_fault(backend, false);
    cf09_max14830_backend_leave(backend);
}

cf09_max14830_backend_result_t cf09_max14830_backend_init(
    cf09_max14830_backend_t *backend,
    const cf09_max14830_backend_platform_ops_t *platform)
{
    cf09_max14830_backend_platform_ops_t selected;

    if (backend == NULL || !cf09_max14830_backend_platform_valid(platform)) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    if (backend->_initialized) {
        return CF09_MAX14830_BACKEND_INVALID_STATE;
    }
    selected = *platform;
    memset(backend, 0, sizeof(*backend));
    backend->_initialized = true;
    backend->_mutating = true;
    backend->_platform = selected;
    backend->_revision_class = CF09_MAX14830_REVISION_REJECTED;
    if (!cf09_max14830_backend_disable_and_assert_reset(backend)) {
        memset(backend, 0, sizeof(*backend));
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    backend->_mutating = false;
    return CF09_MAX14830_BACKEND_OK;
}

cf09_max14830_backend_result_t cf09_max14830_backend_reset_and_probe(
    cf09_max14830_backend_t *backend)
{
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_result_t enable_result;
    cf09_max14830_backend_result_t read_result =
        CF09_MAX14830_BACKEND_NOT_READY;
    cf09_max14830_backend_result_t cleanup_result;
    cf09_max14830_revision_probe_t probe;
    cf09_max14830_revision_class_t revision_class;
    uint8_t rx[2] = {0U, 0U};
    uint8_t revision = 0U;
    size_t rx_len = 0U;
    uint8_t uart;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (backend->_quiesced || backend->_reset_catchup_pending ||
        backend->_outputs_enabled ||
        backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY ||
        cf09_max14830_backend_has_open_channels(backend) ||
        (backend->_ever_probed &&
         backend->_last_probe_device_generation ==
             backend->_device_generation)) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_INVALID_STATE;
    }
    cf09_max14830_backend_clear_attestations(backend);
    backend->_extended_state_unknown = false;
    if (!cf09_max14830_backend_disable_and_assert_reset(backend)) {
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    if (!backend->_platform.set_reset(backend->_platform.user, false)) {
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    backend->_reset_active = false;
    if (!backend->_platform.wait_ready(backend->_platform.user)) {
        bool reset_ok = backend->_platform.set_reset(
            backend->_platform.user, true);
        backend->_reset_active = reset_ok;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return reset_ok ? CF09_MAX14830_BACKEND_NOT_READY :
                          CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    if (cf09_max14830_build_revision_probe(&probe) !=
        CF09_MAX14830_RESULT_OK) {
        (void)cf09_max14830_backend_disable_and_assert_reset(backend);
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }

    enable_result = cf09_max14830_backend_control_frame(
        backend, &probe.enable_extended, NULL, 0U, NULL);
    if (enable_result == CF09_MAX14830_BACKEND_OK) {
        read_result = cf09_max14830_backend_control_frame(
            backend, &probe.read_revid, rx, sizeof(rx), &rx_len);
    }
    cleanup_result = cf09_max14830_backend_control_frame(
        backend, &probe.disable_extended, NULL, 0U, NULL);

    if (cleanup_result != CF09_MAX14830_BACKEND_OK) {
        bool reset_ok;
        backend->_extended_state_unknown = true;
        reset_ok = backend->_platform.set_reset(
            backend->_platform.user, true);
        backend->_reset_active = reset_ok;
        cf09_max14830_backend_latch_fault(backend, true);
        cf09_max14830_backend_leave(backend);
        if (!reset_ok) {
            return CF09_MAX14830_BACKEND_IO_FAILURE;
        }
        return cleanup_result == CF09_MAX14830_BACKEND_IO_UNKNOWN ?
            CF09_MAX14830_BACKEND_IO_UNKNOWN :
            CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    if (enable_result != CF09_MAX14830_BACKEND_OK ||
        read_result != CF09_MAX14830_BACKEND_OK) {
        bool reset_ok = backend->_platform.set_reset(
            backend->_platform.user, true);
        backend->_reset_active = reset_ok;
        cf09_max14830_backend_latch_fault(
            backend,
            enable_result == CF09_MAX14830_BACKEND_IO_UNKNOWN ||
                read_result == CF09_MAX14830_BACKEND_IO_UNKNOWN);
        cf09_max14830_backend_leave(backend);
        if (!reset_ok) {
            return CF09_MAX14830_BACKEND_IO_FAILURE;
        }
        return enable_result == CF09_MAX14830_BACKEND_IO_UNKNOWN ||
                       read_result == CF09_MAX14830_BACKEND_IO_UNKNOWN ?
            CF09_MAX14830_BACKEND_IO_UNKNOWN :
            CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    if (cf09_max14830_decode_single_read_data(
            rx, rx_len, &revision) != CF09_MAX14830_RESULT_OK) {
        (void)cf09_max14830_backend_disable_and_assert_reset(backend);
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    revision_class = cf09_max14830_classify_revision(revision);
    backend->_revision_value = revision;
    backend->_revision_class = revision_class;
    if (revision_class != CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4) {
        bool reset_ok = backend->_platform.set_reset(
            backend->_platform.user, true);
        backend->_reset_active = reset_ok;
        if (!reset_ok) {
            cf09_max14830_backend_latch_fault(backend, false);
        }
        cf09_max14830_backend_leave(backend);
        return reset_ok ? CF09_MAX14830_BACKEND_UNSUPPORTED_SILICON :
                          CF09_MAX14830_BACKEND_IO_FAILURE;
    }

    for (uart = 0U;
         uart < (uint8_t)CF09_MAX14830_UART_COUNT;
         ++uart) {
        uint8_t value = 0U;
        cf09_max14830_backend_result_t result =
            cf09_max14830_backend_read_register(
                backend,
                uart,
                (uint8_t)CF09_MAX14830_REG_ISR,
                &value);
        if (result != CF09_MAX14830_BACKEND_OK) {
            bool reset_ok =
                cf09_max14830_backend_disable_and_assert_reset(backend);
            cf09_max14830_backend_latch_fault(backend, false);
            cf09_max14830_backend_leave(backend);
            return reset_ok ? result : CF09_MAX14830_BACKEND_IO_FAILURE;
        }
        result = cf09_max14830_backend_read_register(
            backend,
            uart,
            (uint8_t)CF09_MAX14830_REG_TX_FIFO_LEVEL,
            &value);
        if (result != CF09_MAX14830_BACKEND_OK || value != 0U) {
            bool reset_ok =
                cf09_max14830_backend_disable_and_assert_reset(backend);
            cf09_max14830_backend_latch_fault(backend, false);
            cf09_max14830_backend_leave(backend);
            if (!reset_ok) {
                return CF09_MAX14830_BACKEND_IO_FAILURE;
            }
            return result == CF09_MAX14830_BACKEND_OK ?
                CF09_MAX14830_BACKEND_NOT_READY : result;
        }
    }

    backend->_qualified_b4 = true;
    backend->_attested_bus_generation = backend->_bus_generation;
    backend->_attested_device_generation = backend->_device_generation;
    backend->_faulted = false;
    backend->_extended_state_unknown = false;
    backend->_ever_probed = true;
    backend->_last_probe_device_generation =
        backend->_device_generation;
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

cf09_max14830_backend_result_t
cf09_max14830_backend_attest_channel_configured(
    cf09_max14830_backend_t *backend,
    uint8_t logical_channel,
    uint32_t resource_generation)
{
    cf09_max14830_backend_result_t entered;
    size_t index;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (!cf09_max14830_backend_logical_to_index(
            logical_channel, &index)) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    backend->_idle_mask = (uint8_t)(
        backend->_idle_mask & (uint8_t)~(uint8_t)(1U << index));
    if (!cf09_max14830_backend_generations_attested(backend) ||
        backend->_faulted || backend->_outputs_enabled ||
        backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY ||
        resource_generation != backend->_resource_generation[index]) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_NOT_READY;
    }
    backend->_configured_mask = (uint8_t)(
        backend->_configured_mask | (uint8_t)(1U << index));
    backend->_idle_mask = (uint8_t)(
        backend->_idle_mask & (uint8_t)~(uint8_t)(1U << index));
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

cf09_max14830_backend_result_t
cf09_max14830_backend_attest_uart3_irq_masked(
    cf09_max14830_backend_t *backend,
    uint32_t device_generation)
{
    cf09_max14830_backend_result_t entered;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (!cf09_max14830_backend_generations_attested(backend) ||
        backend->_faulted || backend->_outputs_enabled ||
        backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY ||
        cf09_max14830_backend_has_open_channels(backend) ||
        device_generation != backend->_device_generation) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_NOT_READY;
    }
    backend->_uart3_irq_masked = true;
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

cf09_max14830_backend_result_t cf09_max14830_backend_verify_channel_idle(
    cf09_max14830_backend_t *backend,
    uint8_t logical_channel,
    uint32_t resource_generation)
{
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_result_t result;
    size_t index;
    uint8_t value = 0U;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (!cf09_max14830_backend_logical_to_index(
            logical_channel, &index)) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    backend->_idle_mask = (uint8_t)(
        backend->_idle_mask & (uint8_t)~(uint8_t)(1U << index));
    if (!cf09_max14830_backend_generations_attested(backend) ||
        backend->_faulted || backend->_outputs_enabled ||
        backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY ||
        backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY ||
        (backend->_configured_mask & (uint8_t)(1U << index)) == 0U ||
        resource_generation != backend->_resource_generation[index]) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_NOT_READY;
    }
    result = cf09_max14830_backend_read_register(
        backend,
        (uint8_t)index,
        (uint8_t)CF09_MAX14830_REG_TX_FIFO_LEVEL,
        &value);
    if (result != CF09_MAX14830_BACKEND_OK || value != 0U) {
        cf09_max14830_backend_leave(backend);
        return result == CF09_MAX14830_BACKEND_OK ?
            CF09_MAX14830_BACKEND_NOT_READY : result;
    }
    backend->_idle_mask = (uint8_t)(
        backend->_idle_mask | (uint8_t)(1U << index));
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

static cf09_a_backend_start_result_t cf09_max14830_backend_core_start_tx(
    void *user,
    const cf09_a_attempt_token_t *token,
    const uint8_t *payload,
    size_t payload_len)
{
    cf09_max14830_backend_t *backend =
        (cf09_max14830_backend_t *)user;
    cf09_max14830_backend_io_result_t io_result;
    cf09_max14830_result_t codec_result;
    cf09_max14830_backend_result_t entered;
    size_t index;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered == CF09_MAX14830_BACKEND_BUSY ?
            CF09_A_BACKEND_BUSY : CF09_A_BACKEND_FAILURE;
    }
    if (token == NULL || payload == NULL || payload_len == 0U ||
        payload_len > (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS ||
        token->payload_len != payload_len ||
        token->payload_fingerprint !=
            cf09_a_payload_fingerprint(payload, payload_len) ||
        !cf09_max14830_backend_token_route_valid(
            backend, token, &index)) {
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_REJECTED;
    }
    if (!cf09_max14830_backend_generations_attested(backend) ||
        backend->_faulted || backend->_extended_state_unknown ||
        backend->_quiesced || !backend->_outputs_enabled ||
        backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY ||
        backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY ||
        (backend->_configured_mask & (uint8_t)(1U << index)) == 0U ||
        (backend->_idle_mask & (uint8_t)(1U << index)) == 0U) {
        cf09_max14830_backend_leave(backend);
        return backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY ||
                       backend->_channels[index].state !=
                           CF09_MAX14830_BACKEND_CHANNEL_EMPTY ?
            CF09_A_BACKEND_BUSY : CF09_A_BACKEND_REJECTED;
    }
    if (backend->_started_count == UINT64_MAX) {
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_FAILURE;
    }

    memset(&backend->_host, 0, sizeof(backend->_host));
    backend->_host.token = *token;
    backend->_host.payload_len = payload_len;
    memcpy(backend->_host.payload, payload, payload_len);
    codec_result = cf09_max14830_build_cf09_tx_fifo_write(
        backend->_revision_class,
        token->logical_channel,
        backend->_host.payload,
        payload_len,
        &backend->_host.frame);
    if (codec_result != CF09_MAX14830_RESULT_OK) {
        memset(&backend->_host, 0, sizeof(backend->_host));
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_REJECTED;
    }
    io_result = backend->_platform.start_transfer(
        backend->_platform.user,
        backend->_host.frame.bytes,
        backend->_host.frame.len);
    if (io_result == CF09_MAX14830_BACKEND_IO_OK) {
        backend->_host.state = CF09_MAX14830_BACKEND_HOST_IN_FLIGHT;
        backend->_started_count++;
        backend->_idle_mask = (uint8_t)(
            backend->_idle_mask & (uint8_t)~(uint8_t)(1U << index));
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_STARTED;
    }
    if (io_result == CF09_MAX14830_BACKEND_IO_UNKNOWN_ACCEPTANCE) {
        backend->_host.state = CF09_MAX14830_BACKEND_HOST_UNCERTAIN;
        backend->_started_count++;
        backend->_idle_mask = (uint8_t)(
            backend->_idle_mask & (uint8_t)~(uint8_t)(1U << index));
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_UNKNOWN_ACCEPTANCE;
    }
    memset(&backend->_host, 0, sizeof(backend->_host));
    if (io_result == CF09_MAX14830_BACKEND_IO_BUSY) {
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_BUSY;
    }
    if (io_result == CF09_MAX14830_BACKEND_IO_REJECTED ||
        io_result == CF09_MAX14830_BACKEND_IO_NOT_COMMITTED) {
        cf09_max14830_backend_leave(backend);
        return CF09_A_BACKEND_REJECTED;
    }
    cf09_max14830_backend_latch_fault(backend, false);
    cf09_max14830_backend_leave(backend);
    return CF09_A_BACKEND_FAILURE;
}

static cf09_max14830_backend_result_t
cf09_max14830_backend_record_host_result_internal(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result)
{
    cf09_max14830_backend_result_t entered;

    if (token == NULL ||
        (result != CF09_A_COMMIT_COMMITTED &&
         result != CF09_A_COMMIT_NOT_COMMITTED &&
         result != CF09_A_COMMIT_UNKNOWN)) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (backend->_host.state !=
            CF09_MAX14830_BACKEND_HOST_IN_FLIGHT ||
        !cf09_a_token_equal(&backend->_host.token, token)) {
        if (backend->_rejected_event_count != UINT64_MAX) {
            backend->_rejected_event_count++;
        }
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_STALE_TOKEN;
    }
    backend->_host.result = result;
    backend->_host.state = CF09_MAX14830_BACKEND_HOST_RESULT_READY;
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

cf09_a_result_t cf09_max14830_backend_dispatch_host_result(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result)
{
    cf09_max14830_backend_result_t backend_result;
    cf09_a_result_t core_result;
    cf09_a_result_t expected_result;
    bool backend_post_ok = false;
    bool backend_owns_token;
    size_t index;

    if (backend == NULL || driver == NULL || token == NULL ||
        !driver->_initialized || driver->_backend.user != backend ||
        !cf09_max14830_backend_token_route_valid(
            backend, token, &index)) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_mutating) {
        return CF09_A_RESULT_REENTRANT;
    }
    backend_owns_token =
        backend->_host.state == CF09_MAX14830_BACKEND_HOST_IN_FLIGHT &&
        cf09_a_token_equal(&backend->_host.token, token);
    if (result != CF09_A_COMMIT_COMMITTED &&
        result != CF09_A_COMMIT_NOT_COMMITTED &&
        result != CF09_A_COMMIT_UNKNOWN) {
        if (backend_owns_token) {
            cf09_max14830_backend_dispatch_fail_closed(backend);
        }
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (!driver->_host_pending || driver->_abort_pending ||
        !cf09_a_token_equal(&driver->_host_pending_token, token) ||
        !driver->_slots[index].occupied ||
        driver->_slots[index].state != CF09_A_ATTEMPT_HOST_PENDING ||
        !cf09_a_token_equal(&driver->_slots[index].token, token) ||
        driver->_resource_generation[index] !=
            backend->_resource_generation[index] ||
        driver->_bus_generation != backend->_bus_generation ||
        driver->_device_generation != backend->_device_generation) {
        if (backend_owns_token) {
            cf09_max14830_backend_dispatch_fail_closed(backend);
        }
        return CF09_A_RESULT_STALE_TOKEN;
    }
    backend_result = cf09_max14830_backend_record_host_result_internal(
        backend, token, result);
    if (backend_result == CF09_MAX14830_BACKEND_STALE_TOKEN) {
        return CF09_A_RESULT_STALE_TOKEN;
    }
    if (backend_result == CF09_MAX14830_BACKEND_BUSY) {
        return CF09_A_RESULT_REENTRANT;
    }
    if (backend_result != CF09_MAX14830_BACKEND_OK) {
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    core_result = cf09_a_on_host_result(driver, token, result);
    expected_result = result == CF09_A_COMMIT_COMMITTED ?
        CF09_A_RESULT_OK :
        (result == CF09_A_COMMIT_NOT_COMMITTED ?
             CF09_A_RESULT_NOT_COMMITTED : CF09_A_RESULT_UNCERTAIN);
    if (result == CF09_A_COMMIT_COMMITTED) {
        backend_post_ok =
            backend->_host.state == CF09_MAX14830_BACKEND_HOST_EMPTY &&
            backend->_channels[index].state ==
                CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING &&
            cf09_a_token_equal(
                &backend->_channels[index].token, token);
    } else if (result == CF09_A_COMMIT_NOT_COMMITTED) {
        backend_post_ok =
            backend->_host.state == CF09_MAX14830_BACKEND_HOST_EMPTY &&
            backend->_channels[index].state ==
                CF09_MAX14830_BACKEND_CHANNEL_EMPTY &&
            (backend->_idle_mask & (uint8_t)(1U << index)) != 0U;
    } else {
        backend_post_ok =
            backend->_host.state == CF09_MAX14830_BACKEND_HOST_EMPTY &&
            backend->_channels[index].state ==
                CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
    }
    if ((core_result == expected_result ||
         (result == CF09_A_COMMIT_UNKNOWN &&
          core_result == CF09_A_RESULT_COUNTER_EXHAUSTED)) &&
        backend_post_ok) {
        return core_result;
    }
    cf09_max14830_backend_dispatch_fail_closed(backend);
    return core_result == expected_result ?
        CF09_A_RESULT_BACKEND_FAILURE : core_result;
}

static bool cf09_max14830_backend_core_close_host_tx(
    void *user,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_backend_t *backend =
        (cf09_max14830_backend_t *)user;
    cf09_max14830_backend_result_t entered;
    size_t index;
    cf09_a_commit_result_t result;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return false;
    }
    if (token == NULL || backend->_host.state !=
            CF09_MAX14830_BACKEND_HOST_RESULT_READY ||
        !cf09_a_token_equal(&backend->_host.token, token) ||
        !cf09_max14830_backend_token_route_valid(
            backend, token, &index) ||
        backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY) {
        cf09_max14830_backend_leave(backend);
        return false;
    }
    result = backend->_host.result;
    if (result == CF09_A_COMMIT_COMMITTED) {
        backend->_channels[index].state =
            CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING;
        backend->_channels[index].token = *token;
    } else if (result == CF09_A_COMMIT_UNKNOWN) {
        backend->_channels[index].state =
            CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        backend->_channels[index].token = *token;
        cf09_max14830_backend_latch_fault(backend, false);
    } else {
        backend->_idle_mask = (uint8_t)(
            backend->_idle_mask | (uint8_t)(1U << index));
    }
    memset(&backend->_host, 0, sizeof(backend->_host));
    cf09_max14830_backend_leave(backend);
    return true;
}

static bool cf09_max14830_backend_core_abort_host_tx(
    void *user,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_backend_t *backend =
        (cf09_max14830_backend_t *)user;
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_channel_slot_t *channel;
    bool ok;
    bool reset_ok;

    if (token == NULL) {
        return false;
    }
    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return false;
    }
    if (backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY &&
        cf09_a_token_equal(&backend->_host.token, token)) {
        if (backend->_host.state ==
                CF09_MAX14830_BACKEND_HOST_RESULT_READY &&
            backend->_host.result == CF09_A_COMMIT_NOT_COMMITTED) {
            size_t index = (size_t)token->physical_channel;
            backend->_idle_mask = (uint8_t)(
                backend->_idle_mask | (uint8_t)(1U << index));
            memset(&backend->_host, 0, sizeof(backend->_host));
            cf09_max14830_backend_leave(backend);
            return true;
        }
        if (backend->_host.state ==
            CF09_MAX14830_BACKEND_HOST_RESULT_READY) {
            bool gate_ok = backend->_platform.set_output_gate(
                backend->_platform.user, false);
            bool abort_ok = backend->_platform.abort_transfer(
                backend->_platform.user);
            bool guards_ok =
                cf09_max14830_backend_quarantine_channels(backend);
            reset_ok = backend->_platform.set_reset(
                backend->_platform.user, true);
            backend->_outputs_enabled = false;
            backend->_reset_active = reset_ok;
            backend->_host.state =
                CF09_MAX14830_BACKEND_HOST_UNCERTAIN;
            cf09_max14830_backend_clear_attestations(backend);
            cf09_max14830_backend_latch_fault(backend, false);
            cf09_max14830_backend_leave(backend);
            return gate_ok && abort_ok && guards_ok && reset_ok;
        }
        ok = backend->_platform.abort_transfer(backend->_platform.user);
        if (ok) {
            memset(&backend->_host, 0, sizeof(backend->_host));
        } else {
            cf09_max14830_backend_latch_fault(backend, false);
        }
        cf09_max14830_backend_leave(backend);
        return ok;
    }
    channel = cf09_max14830_backend_find_channel_by_token(
        backend, token, NULL);
    if (channel == NULL) {
        cf09_max14830_backend_leave(backend);
        return false;
    }
    ok = backend->_platform.set_output_gate(
        backend->_platform.user, false);
    backend->_outputs_enabled = false;
    ok = cf09_max14830_backend_quarantine_channels(backend) && ok;
    reset_ok = backend->_platform.set_reset(
        backend->_platform.user, true);
    backend->_reset_active = reset_ok;
    ok = reset_ok && ok;
    cf09_max14830_backend_clear_attestations(backend);
    cf09_max14830_backend_latch_fault(backend, false);
    cf09_max14830_backend_leave(backend);
    return ok;
}

static bool cf09_max14830_backend_core_set_outputs_enabled(
    void *user,
    bool enabled)
{
    cf09_max14830_backend_t *backend =
        (cf09_max14830_backend_t *)user;
    cf09_max14830_backend_result_t entered;
    bool ok;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return false;
    }
    if (enabled) {
        if (!cf09_max14830_backend_generations_attested(backend) ||
            backend->_faulted || backend->_extended_state_unknown ||
            backend->_quiesced || backend->_reset_active ||
            !backend->_uart3_irq_masked ||
            backend->_configured_mask !=
                (uint8_t)CF09_MAX14830_BACKEND_ALL_CHANNELS_MASK ||
            backend->_idle_mask !=
                (uint8_t)CF09_MAX14830_BACKEND_ALL_CHANNELS_MASK ||
            backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY ||
            cf09_max14830_backend_has_open_channels(backend) ||
            backend->_platform.completion_health(
                backend->_platform.user) !=
                CF09_A_COMPLETION_PATH_HEALTHY) {
            cf09_max14830_backend_leave(backend);
            return false;
        }
    }
    ok = backend->_platform.set_output_gate(
        backend->_platform.user, enabled);
    if (!ok) {
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return false;
    }
    backend->_outputs_enabled = enabled;
    cf09_max14830_backend_leave(backend);
    return true;
}

cf09_max14830_backend_result_t cf09_max14830_backend_observe_tail(
    cf09_max14830_backend_t *backend,
    uint8_t logical_channel,
    cf09_a_completion_source_t source,
    cf09_a_attempt_token_t *token_out)
{
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_result_t result;
    size_t index;
    uint8_t isr = 0U;
    cf09_max14830_backend_channel_slot_t *channel;

    if (token_out == NULL ||
        (source != CF09_A_COMPLETION_IRQ &&
         source != CF09_A_COMPLETION_STATUS_POLL)) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (!cf09_max14830_backend_logical_to_index(
            logical_channel, &index)) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    channel = &backend->_channels[index];
    if (backend->_faulted || backend->_reset_active ||
        backend->_quiesced || !backend->_outputs_enabled ||
        backend->_host.state !=
            CF09_MAX14830_BACKEND_HOST_EMPTY ||
        channel->state != CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING ||
        !cf09_max14830_backend_generations_attested(backend)) {
        cf09_max14830_backend_leave(backend);
        return backend->_host.state !=
                CF09_MAX14830_BACKEND_HOST_EMPTY ?
            CF09_MAX14830_BACKEND_BUSY :
            CF09_MAX14830_BACKEND_NOT_READY;
    }
    if (backend->_tail_count == UINT64_MAX) {
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED;
    }
    result = cf09_max14830_backend_read_register(
        backend,
        (uint8_t)index,
        (uint8_t)CF09_MAX14830_REG_ISR,
        &isr);
    if (result != CF09_MAX14830_BACKEND_OK) {
        channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return result;
    }
    channel->last_isr = isr;
    if ((isr & (uint8_t)~(uint8_t)
             CF09_MAX14830_BACKEND_ISR_ALLOWED_TX_MASK) != 0U) {
        channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    if (!cf09_max14830_isr_reports_tx_fifo_empty(isr)) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_NOT_READY;
    }
    if (!backend->_platform.arm_final_character_guard(
            backend->_platform.user,
            (uint8_t)index,
            &channel->token)) {
        channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    channel->state = CF09_MAX14830_BACKEND_CHANNEL_TAIL_PENDING;
    channel->tail_source = source;
    channel->guard_armed = true;
    backend->_tail_count++;
    *token_out = channel->token;
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

cf09_max14830_backend_result_t cf09_max14830_backend_on_guard_elapsed(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t *source_out)
{
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_channel_slot_t *channel;

    if (token == NULL || source_out == NULL) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    channel = cf09_max14830_backend_find_channel_by_token(
        backend, token, NULL);
    if (channel != NULL &&
        (backend->_faulted || backend->_reset_active ||
         backend->_quiesced || !backend->_outputs_enabled ||
         !cf09_max14830_backend_generations_attested(backend))) {
        if (backend->_rejected_event_count != UINT64_MAX) {
            backend->_rejected_event_count++;
        }
        channel->guard_armed = false;
        channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_NOT_READY;
    }
    if (channel == NULL || channel->state !=
            CF09_MAX14830_BACKEND_CHANNEL_TAIL_PENDING ||
        !channel->guard_armed) {
        if (backend->_rejected_event_count != UINT64_MAX) {
            backend->_rejected_event_count++;
        }
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_STALE_TOKEN;
    }
    /* The one-shot platform event has already been consumed. */
    channel->guard_armed = false;
    if (backend->_completion_count == UINT64_MAX) {
        channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED;
    }
    channel->state = CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY;
    backend->_completion_count++;
    *source_out = channel->tail_source;
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

static cf09_max14830_backend_result_t
cf09_max14830_backend_release_completion_internal(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_channel_slot_t *channel;
    size_t index;

    if (token == NULL) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    channel = cf09_max14830_backend_find_channel_by_token(
        backend, token, &index);
    if (channel == NULL || channel->state !=
            CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_STALE_TOKEN;
    }
    if (backend->_faulted || backend->_reset_active ||
        backend->_quiesced || !backend->_outputs_enabled ||
        !cf09_max14830_backend_generations_attested(backend)) {
        channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_NOT_READY;
    }
    memset(channel, 0, sizeof(*channel));
    backend->_idle_mask = (uint8_t)(
        backend->_idle_mask | (uint8_t)(1U << index));
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

#ifdef CF09_MAX14830_BACKEND_TESTING
cf09_max14830_backend_result_t
cf09_max14830_backend_test_record_host_result(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token,
    cf09_a_commit_result_t result)
{
    return cf09_max14830_backend_record_host_result_internal(
        backend, token, result);
}

cf09_max14830_backend_result_t
cf09_max14830_backend_test_release_completion(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token)
{
    return cf09_max14830_backend_release_completion_internal(
        backend, token);
}
#endif

cf09_a_result_t cf09_max14830_backend_dispatch_completion(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver,
    const cf09_a_attempt_token_t *token,
    cf09_a_completion_source_t source)
{
    cf09_a_result_t core_result;
    cf09_max14830_backend_result_t backend_result;
    size_t index;

    if (backend == NULL || driver == NULL || token == NULL ||
        !driver->_initialized || driver->_mutating ||
        driver->_backend.user != backend ||
        !cf09_max14830_backend_token_route_valid(
            backend, token, &index) ||
        !driver->_slots[index].occupied ||
        driver->_slots[index].state != CF09_A_ATTEMPT_HOST_COMMITTED ||
        !cf09_a_token_equal(&driver->_slots[index].token, token) ||
        backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY ||
        !cf09_a_token_equal(&backend->_channels[index].token, token) ||
        source != backend->_channels[index].tail_source) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    core_result = cf09_a_on_serializer_done_latched(driver, token);
    if (core_result != CF09_A_RESULT_OK) {
        (void)cf09_max14830_backend_mark_timeout(backend, token);
        return core_result;
    }
    core_result = cf09_a_confirm_serializer_complete(
        driver, token, source);
    if (core_result != CF09_A_RESULT_OK) {
        (void)cf09_max14830_backend_mark_timeout(backend, token);
        return core_result;
    }
    backend_result = cf09_max14830_backend_release_completion_internal(
        backend, token);
    if (backend_result != CF09_MAX14830_BACKEND_OK) {
        (void)cf09_max14830_backend_mark_timeout(backend, token);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    return CF09_A_RESULT_OK;
}

cf09_max14830_backend_result_t cf09_max14830_backend_mark_timeout(
    cf09_max14830_backend_t *backend,
    const cf09_a_attempt_token_t *token)
{
    cf09_max14830_backend_result_t entered;
    cf09_max14830_backend_channel_slot_t *channel;
    bool cancel_ok = true;

    if (token == NULL) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    channel = cf09_max14830_backend_find_channel_by_token(
        backend, token, NULL);
    if (channel == NULL ||
        (channel->state != CF09_MAX14830_BACKEND_CHANNEL_SERIALIZING &&
         channel->state != CF09_MAX14830_BACKEND_CHANNEL_TAIL_PENDING &&
         channel->state !=
             CF09_MAX14830_BACKEND_CHANNEL_COMPLETION_READY)) {
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_STALE_TOKEN;
    }
    if (channel->guard_armed) {
        cancel_ok = backend->_platform.cancel_final_character_guard(
            backend->_platform.user,
            token->physical_channel,
            token);
        if (cancel_ok) {
            channel->guard_armed = false;
        }
    }
    channel->state = CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
    cf09_max14830_backend_latch_fault(backend, false);
    cf09_max14830_backend_leave(backend);
    return cancel_ok ? CF09_MAX14830_BACKEND_OK :
                       CF09_MAX14830_BACKEND_IO_FAILURE;
}

static cf09_max14830_backend_result_t
cf09_max14830_backend_notify_reset_internal(
    cf09_max14830_backend_t *backend,
    bool bus_reset)
{
    cf09_max14830_backend_result_t entered;
    bool gate_ok;
    bool abort_ok = true;
    bool guards_ok;
    bool reset_ok;
    bool overflow = false;
    size_t index;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return entered;
    }
    if (backend->_device_generation == UINT32_MAX ||
        (bus_reset && backend->_bus_generation == UINT32_MAX)) {
        overflow = true;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (backend->_resource_generation[index] == UINT32_MAX) {
            overflow = true;
        }
    }

    gate_ok = backend->_platform.set_output_gate(
        backend->_platform.user, false);
    backend->_outputs_enabled = false;
    if (backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY) {
        abort_ok = backend->_platform.abort_transfer(
            backend->_platform.user);
    }
    guards_ok = cf09_max14830_backend_quarantine_channels(backend);
    reset_ok = backend->_platform.set_reset(
        backend->_platform.user, true);
    backend->_reset_active = reset_ok;
    if (!gate_ok || !abort_ok || !guards_ok || !reset_ok) {
        cf09_max14830_backend_clear_attestations(backend);
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_IO_FAILURE;
    }
    if (overflow) {
        cf09_max14830_backend_clear_attestations(backend);
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED;
    }

    backend->_device_generation++;
    if (bus_reset) {
        backend->_bus_generation++;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        backend->_resource_generation[index]++;
    }
    memset(&backend->_host, 0, sizeof(backend->_host));
    memset(backend->_channels, 0, sizeof(backend->_channels));
    cf09_max14830_backend_clear_attestations(backend);
    backend->_reset_active = true;
    backend->_faulted = false;
    backend->_extended_state_unknown = false;
    cf09_max14830_backend_leave(backend);
    return CF09_MAX14830_BACKEND_OK;
}

#ifdef CF09_MAX14830_BACKEND_TESTING
cf09_max14830_backend_result_t
cf09_max14830_backend_test_notify_reset(
    cf09_max14830_backend_t *backend,
    bool bus_reset)
{
    return cf09_max14830_backend_notify_reset_internal(
        backend, bus_reset);
}
#endif

cf09_a_result_t cf09_max14830_backend_dispatch_reset(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver,
    bool bus_reset)
{
    cf09_a_result_t core_result;
    cf09_max14830_backend_result_t backend_result;
    size_t index;

    if (backend == NULL || driver == NULL || !driver->_initialized ||
        driver->_mutating || driver->_backend.user != backend ||
        backend->_reset_catchup_pending) {
        return CF09_A_RESULT_INVALID_ARGUMENT;
    }
    if (driver->_bus_generation != backend->_bus_generation ||
        driver->_device_generation != backend->_device_generation) {
        return CF09_A_RESULT_INVALID_STATE;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (driver->_resource_generation[index] !=
            backend->_resource_generation[index]) {
            return CF09_A_RESULT_INVALID_STATE;
        }
    }
    core_result = bus_reset ? cf09_a_on_bus_reset(driver) :
                              cf09_a_on_device_reset(driver);
    if (core_result == CF09_A_RESULT_COUNTER_EXHAUSTED) {
        cf09_max14830_backend_dispatch_fail_closed(backend);
        return core_result;
    }
    if (core_result != CF09_A_RESULT_OK &&
        core_result != CF09_A_RESULT_UNCERTAIN &&
        core_result != CF09_A_RESULT_BACKEND_FAILURE) {
        return core_result;
    }
    backend->_reset_catchup_pending = true;
    backend->_reset_catchup_bus = bus_reset;
    backend_result = cf09_max14830_backend_notify_reset_internal(
        backend, bus_reset);
    if (backend_result != CF09_MAX14830_BACKEND_OK) {
        return backend_result ==
                       CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED ?
            CF09_A_RESULT_COUNTER_EXHAUSTED :
            CF09_A_RESULT_BACKEND_FAILURE;
    }
    backend->_reset_catchup_pending = false;
    backend->_reset_catchup_bus = false;
    if (driver->_bus_generation != backend->_bus_generation ||
        driver->_device_generation != backend->_device_generation) {
        cf09_max14830_backend_dispatch_fail_closed(backend);
        return CF09_A_RESULT_BACKEND_FAILURE;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (driver->_resource_generation[index] !=
            backend->_resource_generation[index]) {
            cf09_max14830_backend_dispatch_fail_closed(backend);
            return CF09_A_RESULT_BACKEND_FAILURE;
        }
    }
    return core_result;
}

cf09_a_result_t cf09_max14830_backend_retry_reset_catchup(
    cf09_max14830_backend_t *backend,
    cf09_a_driver_t *driver)
{
    cf09_max14830_backend_result_t backend_result;
    uint32_t expected_bus;
    size_t index;

    if (backend == NULL || driver == NULL || !driver->_initialized ||
        driver->_mutating || driver->_backend.user != backend ||
        !backend->_reset_catchup_pending ||
        backend->_device_generation == UINT32_MAX ||
        driver->_device_generation != backend->_device_generation + 1U) {
        return CF09_A_RESULT_INVALID_STATE;
    }
    expected_bus = backend->_bus_generation +
        (backend->_reset_catchup_bus ? 1U : 0U);
    if (driver->_bus_generation != expected_bus) {
        return CF09_A_RESULT_INVALID_STATE;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (backend->_resource_generation[index] == UINT32_MAX ||
            driver->_resource_generation[index] !=
                backend->_resource_generation[index] + 1U) {
            return CF09_A_RESULT_INVALID_STATE;
        }
    }
    backend_result = cf09_max14830_backend_notify_reset_internal(
        backend, backend->_reset_catchup_bus);
    if (backend_result != CF09_MAX14830_BACKEND_OK) {
        return backend_result ==
                       CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED ?
            CF09_A_RESULT_COUNTER_EXHAUSTED :
            CF09_A_RESULT_BACKEND_FAILURE;
    }
    backend->_reset_catchup_pending = false;
    backend->_reset_catchup_bus = false;
    return CF09_A_RESULT_OK;
}

static bool cf09_max14830_backend_core_quiesce(void *user)
{
    cf09_max14830_backend_t *backend =
        (cf09_max14830_backend_t *)user;
    cf09_max14830_backend_result_t entered;
    bool gate_ok;
    bool abort_ok = true;
    bool guards_ok;
    bool reset_ok;
    bool quiesce_ok;

    entered = cf09_max14830_backend_enter(backend);
    if (entered != CF09_MAX14830_BACKEND_OK) {
        return false;
    }
    gate_ok = backend->_platform.set_output_gate(
        backend->_platform.user, false);
    backend->_outputs_enabled = false;
    if (backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY) {
        abort_ok = backend->_platform.abort_transfer(
            backend->_platform.user);
    }
    guards_ok = cf09_max14830_backend_quarantine_channels(backend);
    reset_ok = backend->_platform.set_reset(
        backend->_platform.user, true);
    backend->_reset_active = reset_ok;
    quiesce_ok = backend->_platform.quiesce(
        backend->_platform.user);
    if (!gate_ok || !abort_ok || !guards_ok || !reset_ok ||
        !quiesce_ok) {
        cf09_max14830_backend_clear_attestations(backend);
        cf09_max14830_backend_latch_fault(backend, false);
        cf09_max14830_backend_leave(backend);
        return false;
    }
    memset(&backend->_host, 0, sizeof(backend->_host));
    memset(backend->_channels, 0, sizeof(backend->_channels));
    cf09_max14830_backend_clear_attestations(backend);
    backend->_reset_active = true;
    backend->_quiesced = true;
    cf09_max14830_backend_leave(backend);
    return true;
}

static cf09_a_completion_path_health_t
cf09_max14830_backend_core_completion_health(void *user)
{
    const cf09_max14830_backend_t *backend =
        (const cf09_max14830_backend_t *)user;
    size_t index;

    if (backend == NULL || !backend->_initialized || backend->_faulted ||
        backend->_extended_state_unknown || backend->_quiesced ||
        backend->_reset_catchup_pending ||
        !backend->_outputs_enabled || backend->_reset_active ||
        !cf09_max14830_backend_generations_attested(backend) ||
        !backend->_uart3_irq_masked ||
        backend->_configured_mask !=
            (uint8_t)CF09_MAX14830_BACKEND_ALL_CHANNELS_MASK ||
        backend->_idle_mask !=
            (uint8_t)CF09_MAX14830_BACKEND_ALL_CHANNELS_MASK) {
        return CF09_A_COMPLETION_PATH_UNKNOWN;
    }
    if (backend->_host.state != CF09_MAX14830_BACKEND_HOST_EMPTY) {
        return CF09_A_COMPLETION_PATH_UNKNOWN;
    }
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY) {
            return CF09_A_COMPLETION_PATH_UNKNOWN;
        }
    }
    return backend->_platform.completion_health(
        backend->_platform.user);
}

cf09_a_backend_ops_t cf09_max14830_backend_core_ops(
    cf09_max14830_backend_t *backend)
{
    cf09_a_backend_ops_t ops;

    memset(&ops, 0, sizeof(ops));
    if (backend == NULL || !backend->_initialized) {
        return ops;
    }
    ops.user = backend;
    ops.start_tx = cf09_max14830_backend_core_start_tx;
    ops.close_host_tx = cf09_max14830_backend_core_close_host_tx;
    ops.abort_host_tx = cf09_max14830_backend_core_abort_host_tx;
    ops.set_outputs_enabled =
        cf09_max14830_backend_core_set_outputs_enabled;
    ops.quiesce = cf09_max14830_backend_core_quiesce;
    ops.completion_health =
        cf09_max14830_backend_core_completion_health;
    return ops;
}

cf09_max14830_backend_result_t cf09_max14830_backend_get_generations(
    const cf09_max14830_backend_t *backend,
    uint32_t *bus_generation_out,
    uint32_t *device_generation_out)
{
    if (backend == NULL || !backend->_initialized ||
        bus_generation_out == NULL || device_generation_out == NULL) {
        return CF09_MAX14830_BACKEND_INVALID_ARGUMENT;
    }
    *bus_generation_out = backend->_bus_generation;
    *device_generation_out = backend->_device_generation;
    return CF09_MAX14830_BACKEND_OK;
}

cf09_max14830_backend_channel_state_t
cf09_max14830_backend_channel_state(
    const cf09_max14830_backend_t *backend,
    uint8_t logical_channel)
{
    size_t index;

    if (backend == NULL || !backend->_initialized ||
        !cf09_max14830_backend_logical_to_index(
            logical_channel, &index)) {
        return CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN;
    }
    return backend->_channels[index].state;
}

cf09_max14830_backend_validation_t cf09_max14830_backend_validate(
    const cf09_max14830_backend_t *backend)
{
    cf09_max14830_backend_validation_t validation;
    size_t index;

    memset(&validation, 0, sizeof(validation));
    validation.model_status = CF09_MAX14830_BACKEND_MODEL_STATUS;
    validation.hardware_status = CF09_MAX14830_BACKEND_HARDWARE_STATUS;
    validation.configuration_status = CF09_MAX14830_BACKEND_CONFIG_STATUS;
    validation.uart3_irq_policy =
        CF09_MAX14830_BACKEND_UART3_IRQ_POLICY;
    validation.completion_health = CF09_A_COMPLETION_PATH_UNKNOWN;
    if (backend == NULL || !backend->_initialized) {
        return validation;
    }
    validation.initialized = true;
    validation.qualified_b4 = backend->_qualified_b4;
    validation.outputs_enabled = backend->_outputs_enabled;
    validation.reset_active = backend->_reset_active;
    validation.faulted = backend->_faulted;
    validation.extended_state_unknown =
        backend->_extended_state_unknown;
    validation.quiesced = backend->_quiesced;
    validation.uart3_irq_masked = backend->_uart3_irq_masked;
    validation.reset_catchup_pending =
        backend->_reset_catchup_pending;
    validation.revision_value = backend->_revision_value;
    validation.revision_class = backend->_revision_class;
    validation.configured_mask = backend->_configured_mask;
    validation.idle_mask = backend->_idle_mask;
    validation.bus_generation = backend->_bus_generation;
    validation.device_generation = backend->_device_generation;
    validation.host_state = backend->_host.state;
    validation.started_count = backend->_started_count;
    validation.tail_count = backend->_tail_count;
    validation.completion_count = backend->_completion_count;
    validation.rejected_event_count = backend->_rejected_event_count;
    for (index = 0U;
         index < (size_t)CF09_MAX14830_BACKEND_TX_CHANNELS;
         ++index) {
        if (backend->_channels[index].state !=
            CF09_MAX14830_BACKEND_CHANNEL_EMPTY) {
            validation.active_channels++;
        }
        if (backend->_channels[index].state ==
            CF09_MAX14830_BACKEND_CHANNEL_UNCERTAIN) {
            validation.uncertain_channels++;
        }
    }
    validation.completion_health =
        cf09_max14830_backend_core_completion_health(
            (void *)backend);
    return validation;
}

const char *cf09_max14830_backend_result_name(
    cf09_max14830_backend_result_t result)
{
    switch (result) {
    case CF09_MAX14830_BACKEND_OK:
        return "OK";
    case CF09_MAX14830_BACKEND_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case CF09_MAX14830_BACKEND_INVALID_STATE:
        return "INVALID_STATE";
    case CF09_MAX14830_BACKEND_NOT_READY:
        return "NOT_READY";
    case CF09_MAX14830_BACKEND_BUSY:
        return "BUSY";
    case CF09_MAX14830_BACKEND_STALE_TOKEN:
        return "STALE_TOKEN";
    case CF09_MAX14830_BACKEND_UNSUPPORTED_SILICON:
        return "UNSUPPORTED_SILICON";
    case CF09_MAX14830_BACKEND_IO_FAILURE:
        return "IO_FAILURE";
    case CF09_MAX14830_BACKEND_IO_UNKNOWN:
        return "IO_UNKNOWN";
    case CF09_MAX14830_BACKEND_COUNTER_EXHAUSTED:
        return "COUNTER_EXHAUSTED";
    default:
        return "INVALID_ENUM";
    }
}
