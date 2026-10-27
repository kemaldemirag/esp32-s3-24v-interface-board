#include "cf09_uart_bridge_fake_v1.h"

#include <stdlib.h>
#include <string.h>

static cf09_a_backend_start_result_t cf09_a_fake_start_tx(
    void *user,
    const cf09_a_attempt_token_t *token,
    const uint8_t *payload,
    size_t payload_len)
{
    cf09_a_fake_backend_t *fake = (cf09_a_fake_backend_t *)user;
    cf09_a_backend_start_result_t selected;
    const cf09_a_route_t *route;
    uint8_t *payload_copy = NULL;

    if (fake == NULL || token == NULL ||
        (payload_len != 0U && payload == NULL)) {
        return CF09_A_BACKEND_REJECTED;
    }
    if (fake->active) {
        return CF09_A_BACKEND_BUSY;
    }
    selected = fake->next_start_result;
    fake->next_start_result = CF09_A_BACKEND_STARTED;
    if (selected == CF09_A_BACKEND_BUSY ||
        selected == CF09_A_BACKEND_REJECTED) {
        return selected;
    }
    if (selected == CF09_A_BACKEND_FAILURE) {
        return selected;
    }

    route = cf09_a_route_for_logical(token->logical_channel);
    if (route == NULL || token->serializer_id != route->serializer_id ||
        token->resource_id != route->resource_id ||
        token->physical_channel != route->physical_channel ||
        token->physical_channel ==
            (uint8_t)CF09_A_RX_RESERVED_PHYSICAL_CHANNEL ||
        token->device_id != (uint8_t)CF09_A_DEVICE_ID ||
        token->chip_select_id != (uint8_t)CF09_A_CHIP_SELECT_ID ||
        token->payload_len != payload_len ||
        token->payload_fingerprint !=
            cf09_a_payload_fingerprint(payload, payload_len)) {
        return CF09_A_BACKEND_REJECTED;
    }

    if (payload_len != 0U) {
        payload_copy = (uint8_t *)malloc(payload_len);
        if (payload_copy == NULL) {
            return CF09_A_BACKEND_FAILURE;
        }
        memcpy(payload_copy, payload, payload_len);
    }

    fake->active = true;
    fake->active_token = *token;
    fake->active_payload = payload_copy;
    fake->observed_payload_len = payload_len;
    fake->observed_payload_fingerprint = token->payload_fingerprint;
    fake->channel_seen[token->physical_channel] = true;
    fake->last_started_by_channel[token->physical_channel] = *token;
    fake->last_payload_fingerprint_by_channel[token->physical_channel] =
        token->payload_fingerprint;
    fake->start_count++;
    return selected;
}

static bool cf09_a_fake_close_host_tx(
    void *user,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_fake_backend_t *fake = (cf09_a_fake_backend_t *)user;

    if (fake == NULL || token == NULL || !fake->active ||
        !cf09_a_token_equal(&fake->active_token, token) ||
        fake->observed_payload_fingerprint != cf09_a_payload_fingerprint(
            fake->active_payload,
            fake->observed_payload_len) ||
        !fake->close_succeeds) {
        return false;
    }
    fake->active = false;
    free(fake->active_payload);
    fake->active_payload = NULL;
    memset(&fake->active_token, 0, sizeof(fake->active_token));
    fake->close_count++;
    return true;
}

static bool cf09_a_fake_abort_host_tx(
    void *user,
    const cf09_a_attempt_token_t *token)
{
    cf09_a_fake_backend_t *fake = (cf09_a_fake_backend_t *)user;

    if (fake == NULL || token == NULL || !fake->active ||
        !cf09_a_token_equal(&fake->active_token, token) ||
        !fake->abort_succeeds) {
        return false;
    }
    fake->active = false;
    free(fake->active_payload);
    fake->active_payload = NULL;
    memset(&fake->active_token, 0, sizeof(fake->active_token));
    fake->abort_count++;
    return true;
}

static bool cf09_a_fake_set_outputs_enabled(void *user, bool enabled)
{
    cf09_a_fake_backend_t *fake = (cf09_a_fake_backend_t *)user;

    if (fake == NULL || !fake->output_change_succeeds) {
        return false;
    }
    fake->outputs_enabled = enabled;
    if (enabled) {
        fake->enable_count++;
    } else {
        fake->disable_count++;
    }
    return true;
}

static bool cf09_a_fake_quiesce(void *user)
{
    cf09_a_fake_backend_t *fake = (cf09_a_fake_backend_t *)user;

    if (fake == NULL || fake->active || !fake->quiesce_succeeds) {
        return false;
    }
    fake->quiesced = true;
    fake->quiesce_count++;
    return true;
}

static cf09_a_completion_path_health_t cf09_a_fake_completion_health(
    void *user)
{
    const cf09_a_fake_backend_t *fake =
        (const cf09_a_fake_backend_t *)user;

    if (fake == NULL) {
        return CF09_A_COMPLETION_PATH_UNKNOWN;
    }
    return fake->completion_path_health;
}

void cf09_a_fake_backend_init(cf09_a_fake_backend_t *fake)
{
    if (fake == NULL) {
        return;
    }
    memset(fake, 0, sizeof(*fake));
    fake->close_succeeds = true;
    fake->abort_succeeds = true;
    fake->output_change_succeeds = true;
    fake->quiesce_succeeds = true;
    fake->next_start_result = CF09_A_BACKEND_STARTED;
    fake->completion_path_health = CF09_A_COMPLETION_PATH_HEALTHY;
}

cf09_a_backend_ops_t cf09_a_fake_backend_ops(
    cf09_a_fake_backend_t *fake)
{
    cf09_a_backend_ops_t ops;

    memset(&ops, 0, sizeof(ops));
    ops.user = fake;
    ops.start_tx = cf09_a_fake_start_tx;
    ops.close_host_tx = cf09_a_fake_close_host_tx;
    ops.abort_host_tx = cf09_a_fake_abort_host_tx;
    ops.set_outputs_enabled = cf09_a_fake_set_outputs_enabled;
    ops.quiesce = cf09_a_fake_quiesce;
    ops.completion_health = cf09_a_fake_completion_health;
    return ops;
}

void cf09_a_fake_set_next_start_result(
    cf09_a_fake_backend_t *fake,
    cf09_a_backend_start_result_t result)
{
    if (fake != NULL) {
        fake->next_start_result = result;
    }
}

void cf09_a_fake_set_close_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds)
{
    if (fake != NULL) {
        fake->close_succeeds = succeeds;
    }
}

void cf09_a_fake_set_abort_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds)
{
    if (fake != NULL) {
        fake->abort_succeeds = succeeds;
    }
}

void cf09_a_fake_set_output_change_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds)
{
    if (fake != NULL) {
        fake->output_change_succeeds = succeeds;
    }
}

void cf09_a_fake_set_quiesce_succeeds(
    cf09_a_fake_backend_t *fake,
    bool succeeds)
{
    if (fake != NULL) {
        fake->quiesce_succeeds = succeeds;
    }
}

void cf09_a_fake_set_completion_path_health(
    cf09_a_fake_backend_t *fake,
    cf09_a_completion_path_health_t health)
{
    if (fake != NULL) {
        fake->completion_path_health = health;
    }
}
