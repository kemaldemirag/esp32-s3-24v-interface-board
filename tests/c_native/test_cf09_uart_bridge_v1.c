#include "cf09_uart_bridge_v1.h"
#include "cf09_uart_bridge_fake_v1.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                    \
        }                                                                    \
    } while (0)

typedef bool (*cf09_test_fn)(void);

typedef struct {
    const char *name;
    cf09_test_fn run;
} cf09_test_case_t;

static cf09_a_session_id_t test_session(uint64_t low)
{
    cf09_a_session_id_t session;

    session.high = UINT64_C(0x434630394D4F4445);
    session.low = low;
    return session;
}

static cf09_a_tx_request_t test_request(
    uint8_t logical_channel,
    uint32_t epoch,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_len)
{
    cf09_a_tx_request_t request;

    request.frame.epoch = epoch;
    request.frame.sequence = sequence;
    request.logical_channel = logical_channel;
    request.payload = payload;
    request.payload_len = payload_len;
    return request;
}

static bool make_ready(
    cf09_a_driver_t *driver,
    cf09_a_fake_backend_t *fake,
    uint64_t session_low)
{
    cf09_a_backend_ops_t ops;
    uint8_t physical_channel;

    memset(driver, 0, sizeof(*driver));
    cf09_a_fake_backend_init(fake);
    ops = cf09_a_fake_backend_ops(fake);
    CHECK(cf09_a_driver_init(driver, test_session(session_low), &ops) ==
          CF09_A_RESULT_OK);
    CHECK(!fake->outputs_enabled);
    CHECK(cf09_a_confirm_bridge_up(driver) == CF09_A_RESULT_OK);
    for (physical_channel = 0U;
         physical_channel < (uint8_t)CF09_A_EXTERNAL_TX_COUNT;
         ++physical_channel) {
        uint32_t generation = UINT32_MAX;
        CHECK(cf09_a_get_resource_generation(
                  driver,
                  physical_channel,
                  &generation) == CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_configured(
                  driver,
                  physical_channel,
                  generation) == CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_idle(
                  driver,
                  physical_channel,
                  generation) == CF09_A_RESULT_OK);
    }
    CHECK(cf09_a_arm_outputs(driver) == CF09_A_RESULT_OK);
    CHECK(fake->outputs_enabled);
    return true;
}

static bool test_routes_and_evidence_boundary(void)
{
    const cf09_a_route_t *route2 = cf09_a_route_for_logical(2U);
    const cf09_a_route_t *route3 = cf09_a_route_for_logical(3U);
    const cf09_a_route_t *route4 = cf09_a_route_for_logical(4U);

    CHECK(cf09_a_route_for_logical(0U) == NULL);
    CHECK(cf09_a_route_for_logical(1U) == NULL);
    CHECK(cf09_a_route_for_logical(5U) == NULL);
    CHECK(route2 != NULL && route2->physical_channel == 0U);
    CHECK(route3 != NULL && route3->physical_channel == 1U);
    CHECK(route4 != NULL && route4->physical_channel == 2U);
    CHECK(route2->serializer_id == 2U && route2->resource_id == 0U);
    CHECK(route3->serializer_id == 3U && route3->resource_id == 1U);
    CHECK(route4->serializer_id == 4U && route4->resource_id == 2U);
    CHECK(strcmp(route4->ttl_endpoint, "TTL4") == 0);
    CHECK(strcmp(route4->rs232_endpoint, "RS2324") == 0);
    CHECK((uint8_t)CF09_A_RX_RESERVED_PHYSICAL_CHANNEL == 3U);
    CHECK(route4->physical_channel !=
          (uint8_t)CF09_A_RX_RESERVED_PHYSICAL_CHANNEL);
    CHECK(strcmp(CF09_A_MODEL_CLASS, "ASSUMPTION / MODEL_ONLY") == 0);
    CHECK(strcmp(CF09_A_HARDWARE_STATUS,
                 "NOT_RUN / HARDWARE_REQUIRED") == 0);
    CHECK(strcmp(CF09_A_RX_STATUS,
                 "RESERVATION_ONLY / RX_PATH_NOT_IMPLEMENTED / NOT_RUN") == 0);
    return true;
}

static bool test_bringup_requires_config_idle_and_arm(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    cf09_a_backend_ops_t ops;
    const uint8_t payload[] = {0x11U};
    cf09_a_tx_request_t request = test_request(2U, 1U, 1U, payload, 1U);
    cf09_a_attempt_token_t token;
    uint8_t physical_channel;

    memset(&driver, 0, sizeof(driver));
    cf09_a_fake_backend_init(&fake);
    ops = cf09_a_fake_backend_ops(&fake);
    CHECK(cf09_a_driver_init(&driver, test_session(1U), &ops) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_begin_tx(&driver, &request, &token) ==
          CF09_A_RESULT_NOT_READY);
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_NOT_READY);
    CHECK(cf09_a_confirm_bridge_up(&driver) == CF09_A_RESULT_OK);
    for (physical_channel = 0U; physical_channel < 2U; ++physical_channel) {
        CHECK(cf09_a_mark_resource_configured(
                  &driver,
                  physical_channel,
                  0U) == CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_idle(
                  &driver,
                  physical_channel,
                  0U) == CF09_A_RESULT_OK);
    }
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_NOT_READY);
    CHECK(cf09_a_mark_resource_configured(&driver, 2U, 0U) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_mark_resource_idle(&driver, 2U, 0U) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_mark_resource_configured(&driver, 0U, 0U) ==
          CF09_A_RESULT_INVALID_STATE);
    CHECK(cf09_a_mark_resource_idle(&driver, 0U, 0U) ==
          CF09_A_RESULT_INVALID_STATE);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_NOT_RUN);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_commit_latch_and_completion_are_distinct(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'p', 'h', 'a', 's', 'e'};
    cf09_a_tx_request_t request = test_request(
        2U, 3U, 7U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;
    cf09_a_attempt_state_t state;
    cf09_a_validation_t validation;

    CHECK(make_ready(&driver, &fake, 2U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(fake.active);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(!fake.active);
    CHECK(cf09_a_get_attempt_state(&driver, 2U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_HOST_COMMITTED);
    CHECK(cf09_a_validate(&driver).completed_count == 0U);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &token,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_STALE_TOKEN);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_get_attempt_state(&driver, 2U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_SERIALIZER_DONE_LATCHED);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &token,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &token,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_STALE_TOKEN);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_STALE_TOKEN);
    validation = cf09_a_validate(&driver);
    CHECK(validation.model_status == CF09_A_MODEL_NOT_READY);
    CHECK(validation.completed_route_mask == 1U);
    CHECK(validation.completed_count == 1U);
    CHECK(strcmp(validation.hardware_status,
                 "NOT_RUN / HARDWARE_REQUIRED") == 0);
    CHECK(strcmp(validation.connector_delivery_status,
                 "NOT_RUN / NOT_PROVEN") == 0);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_single_host_bus_owner(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t p2[] = {2U};
    const uint8_t p3[] = {3U};
    cf09_a_tx_request_t request2 = test_request(2U, 1U, 2U, p2, 1U);
    cf09_a_tx_request_t request3 = test_request(3U, 1U, 3U, p3, 1U);
    cf09_a_attempt_token_t token2;
    cf09_a_attempt_token_t token3;

    CHECK(make_ready(&driver, &fake, 3U));
    CHECK(cf09_a_begin_tx(&driver, &request2, &token2) == CF09_A_RESULT_OK);
    CHECK(cf09_a_begin_tx(&driver, &request3, &token3) ==
          CF09_A_RESULT_BUSY);
    CHECK(fake.start_count == 1U);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token2,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    CHECK(cf09_a_begin_tx(&driver, &request3, &token3) == CF09_A_RESULT_OK);
    CHECK(token3.physical_channel == 1U);
    CHECK(fake.start_count == 2U);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token3,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    CHECK(cf09_a_discard_not_committed(&driver, &token2) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_discard_not_committed(&driver, &token3) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_not_committed_requires_fresh_token(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'r', 'e', 't', 'r', 'y'};
    const uint8_t changed[] = {'o', 't', 'h', 'e', 'r'};
    cf09_a_tx_request_t request = test_request(
        4U, 2U, 9U, payload, sizeof(payload));
    cf09_a_attempt_token_t first;
    cf09_a_attempt_token_t second;

    CHECK(make_ready(&driver, &fake, 4U));
    CHECK(cf09_a_begin_tx(&driver, &request, &first) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &first,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    CHECK(cf09_a_retry_not_committed(
              &driver,
              &first,
              changed,
              sizeof(changed),
              &second) == CF09_A_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_a_retry_not_committed(
              &driver,
              &first,
              payload,
              sizeof(payload),
              &second) == CF09_A_RESULT_OK);
    CHECK(second.attempt_no == first.attempt_no + 1U);
    CHECK(second.transfer_no > first.transfer_no);
    CHECK(!cf09_a_token_equal(&first, &second));
    CHECK(cf09_a_on_host_result(
              &driver,
              &first,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    CHECK(fake.active);
    CHECK(cf09_a_on_host_result(
              &driver,
              &second,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &second) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &second,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_OK);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_NOT_READY);
    return true;
}

static bool test_unknown_commit_is_uncertain_without_retry(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'u', 'n', 'k'};
    cf09_a_tx_request_t request = test_request(
        3U, 4U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t retry_token;

    CHECK(make_ready(&driver, &fake, 5U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_UNKNOWN) == CF09_A_RESULT_UNCERTAIN);
    CHECK(cf09_a_retry_not_committed(
              &driver,
              &token,
              payload,
              sizeof(payload),
              &retry_token) == CF09_A_RESULT_INVALID_STATE);
    CHECK(cf09_a_begin_tx(&driver, &request, &retry_token) ==
          CF09_A_RESULT_BUSY);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_STALE_TOKEN);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_UNCERTAIN);
    CHECK(cf09_a_reconcile_uncertain(&driver, &token) == CF09_A_RESULT_OK);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_UNCERTAIN);
    return true;
}

static bool test_three_channels_complete_in_reverse_order(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payloads[3][2] = {{2U, 0U}, {3U, 0U}, {4U, 0U}};
    cf09_a_attempt_token_t tokens[3];
    size_t index;

    CHECK(make_ready(&driver, &fake, 6U));
    for (index = 0U; index < 3U; ++index) {
        cf09_a_tx_request_t request = test_request(
            (uint8_t)(index + 2U),
            1U,
            (uint32_t)index,
            payloads[index],
            sizeof(payloads[index]));
        CHECK(cf09_a_begin_tx(&driver, &request, &tokens[index]) ==
              CF09_A_RESULT_OK);
        CHECK(cf09_a_on_host_result(
                  &driver,
                  &tokens[index],
                  CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    }
    for (index = 0U; index < 3U; ++index) {
        CHECK(fake.channel_seen[index]);
        CHECK(cf09_a_token_equal(
                  &fake.last_started_by_channel[index],
                  &tokens[index]));
        CHECK(fake.last_payload_fingerprint_by_channel[index] ==
              cf09_a_payload_fingerprint(
                  payloads[index],
                  sizeof(payloads[index])));
    }
    for (index = 3U; index > 0U; --index) {
        CHECK(cf09_a_on_serializer_done_latched(
                  &driver,
                  &tokens[index - 1U]) == CF09_A_RESULT_OK);
    }
    for (index = 3U; index > 0U; --index) {
        CHECK(cf09_a_confirm_serializer_complete(
                  &driver,
                  &tokens[index - 1U],
                  CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_OK);
    }
    CHECK(cf09_a_validate(&driver).completed_count == 3U);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_PASS);
    driver._slots[0].state = (cf09_a_attempt_state_t)99;
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_ERROR);
    driver._slots[0].state = CF09_A_ATTEMPT_COMPLETE;
    cf09_a_fake_set_completion_path_health(
        &fake,
        CF09_A_COMPLETION_PATH_IRQ_STUCK);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_NOT_READY);
    cf09_a_fake_set_completion_path_health(
        &fake,
        CF09_A_COMPLETION_PATH_HEALTHY);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_PASS);
    CHECK(cf09_a_on_device_reset(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_release_reset(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_bridge_up(&driver) == CF09_A_RESULT_OK);
    for (index = 0U; index < 3U; ++index) {
        CHECK(cf09_a_mark_resource_configured(
                  &driver,
                  (uint8_t)index,
                  1U) == CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_idle(
                  &driver,
                  (uint8_t)index,
                  1U) == CF09_A_RESULT_OK);
    }
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_validate(&driver).completed_route_mask == 0U);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_NOT_READY);
    return true;
}

static bool test_status_poll_is_explicit_completion_fallback(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'p', 'o', 'l', 'l'};
    cf09_a_tx_request_t request = test_request(
        4U, 1U, 4U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 7U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &token,
              CF09_A_COMPLETION_STATUS_POLL) == CF09_A_RESULT_OK);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_NOT_READY);
    return true;
}

static bool test_timeout_quarantines_late_completion(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'t', 'i', 'm', 'e'};
    cf09_a_tx_request_t request = test_request(
        2U, 8U, 8U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 8U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_on_completion_timeout(&driver, &token) ==
          CF09_A_RESULT_UNCERTAIN);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &token,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_STALE_TOKEN);
    CHECK(cf09_a_validate(&driver).completed_count == 0U);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_UNCERTAIN);
    return true;
}

static bool test_device_reset_quarantines_pending_and_inflight(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t p2[] = {2U, 2U};
    const uint8_t p3[] = {3U, 3U};
    cf09_a_tx_request_t request2 = test_request(2U, 1U, 2U, p2, 2U);
    cf09_a_tx_request_t request3 = test_request(3U, 1U, 3U, p3, 2U);
    cf09_a_attempt_token_t token2;
    cf09_a_attempt_token_t token3;
    cf09_a_attempt_state_t state;
    uint8_t physical_channel;

    CHECK(make_ready(&driver, &fake, 9U));
    CHECK(cf09_a_begin_tx(&driver, &request2, &token2) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token2,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(cf09_a_begin_tx(&driver, &request3, &token3) == CF09_A_RESULT_OK);
    CHECK(fake.active);
    CHECK(cf09_a_on_device_reset(&driver) == CF09_A_RESULT_UNCERTAIN);
    CHECK(!fake.active);
    CHECK(!fake.outputs_enabled);
    CHECK(cf09_a_get_attempt_state(&driver, 2U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_UNCERTAIN);
    CHECK(cf09_a_get_attempt_state(&driver, 3U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_UNCERTAIN);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token2) ==
          CF09_A_RESULT_STALE_TOKEN);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token3,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    for (physical_channel = 0U;
         physical_channel < (uint8_t)CF09_A_EXTERNAL_TX_COUNT;
         ++physical_channel) {
        uint32_t generation = 0U;
        CHECK(cf09_a_get_resource_generation(
                  &driver,
                  physical_channel,
                  &generation) == CF09_A_RESULT_OK);
        CHECK(generation == 1U);
    }
    CHECK(cf09_a_release_reset(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_bridge_up(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_NOT_READY);
    CHECK(cf09_a_reconcile_uncertain(&driver, &token2) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_reconcile_uncertain(&driver, &token3) ==
          CF09_A_RESULT_OK);
    for (physical_channel = 0U;
         physical_channel < (uint8_t)CF09_A_EXTERNAL_TX_COUNT;
         ++physical_channel) {
        CHECK(cf09_a_mark_resource_configured(
                  &driver,
                  physical_channel,
                  1U) == CF09_A_RESULT_OK);
        CHECK(cf09_a_mark_resource_idle(
                  &driver,
                  physical_channel,
                  1U) == CF09_A_RESULT_OK);
    }
    CHECK(cf09_a_arm_outputs(&driver) == CF09_A_RESULT_OK);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_UNCERTAIN);
    return true;
}

static bool test_bus_reset_invalidates_old_generation(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'b', 'u', 's'};
    cf09_a_tx_request_t request = test_request(
        4U, 1U, 9U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 10U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_bus_reset(&driver) == CF09_A_RESULT_UNCERTAIN);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_STALE_TOKEN);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_UNCERTAIN);
    return true;
}

static bool test_forged_tokens_fail_closed(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'f', 'o', 'r', 'g', 'e'};
    cf09_a_tx_request_t request = test_request(
        2U, 1U, 2U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;
    cf09_a_attempt_token_t forged;

    CHECK(make_ready(&driver, &fake, 11U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    forged = token;
    forged.session.low++;
    CHECK(cf09_a_on_host_result(
              &driver,
              &forged,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    forged = token;
    forged.logical_channel = UINT8_MAX;
    CHECK(cf09_a_on_host_result(
              &driver,
              &forged,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    forged = token;
    forged.resource_generation++;
    CHECK(cf09_a_on_host_result(
              &driver,
              &forged,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    forged = token;
    forged.payload_fingerprint++;
    CHECK(cf09_a_on_host_result(
              &driver,
              &forged,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    CHECK(fake.active);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    CHECK(!fake.active);
    return true;
}

static bool test_backend_busy_and_reject_do_not_mutate_slot(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'b'};
    cf09_a_tx_request_t request = test_request(2U, 1U, 1U, payload, 1U);
    cf09_a_attempt_token_t token;
    cf09_a_attempt_state_t state;

    CHECK(make_ready(&driver, &fake, 12U));
    cf09_a_fake_set_next_start_result(&fake, CF09_A_BACKEND_BUSY);
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_BUSY);
    CHECK(cf09_a_get_attempt_state(&driver, 2U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_EMPTY);
    cf09_a_fake_set_next_start_result(&fake, CF09_A_BACKEND_REJECTED);
    CHECK(cf09_a_begin_tx(&driver, &request, &token) ==
          CF09_A_RESULT_BACKEND_REJECTED);
    CHECK(cf09_a_get_attempt_state(&driver, 2U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_EMPTY);
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(token.transfer_no == 1U);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    CHECK(cf09_a_discard_not_committed(&driver, &token) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_backend_close_failure_is_fail_closed(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'c', 'l', 'o', 's', 'e'};
    cf09_a_tx_request_t request = test_request(
        3U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;
    cf09_a_attempt_state_t state;

    CHECK(make_ready(&driver, &fake, 13U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    cf09_a_fake_set_close_succeeds(&fake, false);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) ==
          CF09_A_RESULT_BACKEND_FAILURE);
    CHECK(!fake.active);
    CHECK(!fake.outputs_enabled);
    CHECK(fake.abort_count == 1U);
    CHECK(cf09_a_get_attempt_state(&driver, 3U, &state) ==
          CF09_A_RESULT_OK);
    CHECK(state == CF09_A_ATTEMPT_UNCERTAIN);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_ERROR);
    return true;
}

static bool test_zero_length_payload_and_null_bounds(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    cf09_a_tx_request_t invalid = test_request(2U, 1U, 1U, NULL, 1U);
    cf09_a_tx_request_t empty = test_request(2U, 1U, 2U, NULL, 0U);
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 14U));
    CHECK(cf09_a_begin_tx(&driver, &invalid, &token) ==
          CF09_A_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_a_begin_tx(&driver, &empty, &token) == CF09_A_RESULT_OK);
    CHECK(token.payload_len == 0U);
    CHECK(fake.observed_payload_len == 0U);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    return true;
}

static bool test_deinit_aborts_pending_and_late_callback_is_invalid(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'d', 'o', 'n', 'e'};
    cf09_a_tx_request_t request = test_request(
        4U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 15U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_UNCERTAIN);
    CHECK(!fake.active);
    CHECK(!fake.outputs_enabled);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) ==
          CF09_A_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_INVALID_ARGUMENT);
    return true;
}

static bool test_counter_wrap_attempts_fail_closed(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'w', 'r', 'a', 'p'};
    cf09_a_tx_request_t request = test_request(
        2U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 16U));
    driver._last_transfer_no = UINT64_MAX;
    CHECK(cf09_a_begin_tx(&driver, &request, &token) ==
          CF09_A_RESULT_COUNTER_EXHAUSTED);
    CHECK(fake.start_count == 0U);
    CHECK(!fake.outputs_enabled);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_ERROR);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_generation_wrap_reset_fails_closed(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;

    CHECK(make_ready(&driver, &fake, 17U));
    driver._device_generation = UINT32_MAX;
    CHECK(cf09_a_on_device_reset(&driver) ==
          CF09_A_RESULT_COUNTER_EXHAUSTED);
    CHECK(driver._device_generation == UINT32_MAX);
    CHECK(!fake.outputs_enabled);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_ERROR);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_unknown_backend_acceptance_disables_outputs(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'i', 'n', 'v'};
    cf09_a_tx_request_t request = test_request(
        2U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 18U));
    cf09_a_fake_set_abort_succeeds(&fake, false);
    cf09_a_fake_set_next_start_result(
        &fake,
        CF09_A_BACKEND_UNKNOWN_ACCEPTANCE);
    CHECK(cf09_a_begin_tx(&driver, &request, &token) ==
          CF09_A_RESULT_UNCERTAIN);
    CHECK(token.transfer_no != 0U);
    CHECK(driver._abort_pending);
    CHECK(fake.active);
    CHECK(!fake.outputs_enabled);
    CHECK(cf09_a_validate(&driver).model_status == CF09_A_MODEL_ERROR);
    cf09_a_fake_set_abort_succeeds(&fake, true);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_UNCERTAIN);
    CHECK(!fake.active);
    return true;
}

static bool test_double_init_is_rejected_without_losing_backend(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t first_fake;
    cf09_a_fake_backend_t second_fake;
    cf09_a_backend_ops_t second_ops;

    CHECK(make_ready(&driver, &first_fake, 19U));
    cf09_a_fake_backend_init(&second_fake);
    second_ops = cf09_a_fake_backend_ops(&second_fake);
    CHECK(cf09_a_driver_init(&driver, test_session(20U), &second_ops) ==
          CF09_A_RESULT_INVALID_STATE);
    CHECK(first_fake.outputs_enabled);
    CHECK(!second_fake.outputs_enabled);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    CHECK(!first_fake.outputs_enabled);
    return true;
}

static bool test_deinit_failures_retain_recoverable_context(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'s', 't', 'o', 'p'};
    cf09_a_tx_request_t request = test_request(
        2U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 21U));
    cf09_a_fake_set_output_change_succeeds(&fake, false);
    CHECK(cf09_a_driver_deinit(&driver) ==
          CF09_A_RESULT_BACKEND_FAILURE);
    CHECK(driver._initialized);
    CHECK(fake.outputs_enabled);
    cf09_a_fake_set_output_change_succeeds(&fake, true);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);

    CHECK(make_ready(&driver, &fake, 22U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    cf09_a_fake_set_abort_succeeds(&fake, false);
    CHECK(cf09_a_driver_deinit(&driver) ==
          CF09_A_RESULT_BACKEND_FAILURE);
    CHECK(driver._initialized);
    CHECK(driver._abort_pending);
    CHECK(fake.active);
    cf09_a_fake_set_abort_succeeds(&fake, true);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_UNCERTAIN);
    CHECK(!fake.active);

    CHECK(make_ready(&driver, &fake, 23U));
    cf09_a_fake_set_quiesce_succeeds(&fake, false);
    CHECK(cf09_a_driver_deinit(&driver) ==
          CF09_A_RESULT_BACKEND_FAILURE);
    CHECK(driver._initialized);
    cf09_a_fake_set_quiesce_succeeds(&fake, true);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    CHECK(fake.quiesced);
    return true;
}

static bool test_backend_owned_payload_survives_caller_mutation(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    uint8_t payload[] = {'c', 'o', 'p', 'y'};
    cf09_a_tx_request_t request = test_request(
        3U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 24U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    payload[0] ^= 0x01U;
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(!fake.active);
    CHECK(fake.outputs_enabled);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &token) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &token,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_OK);
    CHECK(cf09_a_validate(&driver).model_status ==
          CF09_A_MODEL_NOT_READY);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_frame_identity_must_increase_after_reap(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'s', 'e', 'q'};
    cf09_a_tx_request_t first_request = test_request(
        2U, 3U, 7U, payload, sizeof(payload));
    cf09_a_tx_request_t next_request = test_request(
        2U, 3U, 8U, payload, sizeof(payload));
    cf09_a_attempt_token_t first;
    cf09_a_attempt_token_t next;

    CHECK(make_ready(&driver, &fake, 25U));
    CHECK(cf09_a_begin_tx(&driver, &first_request, &first) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &first,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_OK);
    CHECK(cf09_a_on_serializer_done_latched(&driver, &first) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_confirm_serializer_complete(
              &driver,
              &first,
              CF09_A_COMPLETION_IRQ) == CF09_A_RESULT_OK);
    CHECK(cf09_a_reap_complete(&driver, &first) == CF09_A_RESULT_OK);
    CHECK(cf09_a_begin_tx(&driver, &first_request, &next) ==
          CF09_A_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_a_begin_tx(&driver, &next_request, &next) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_on_host_result(
              &driver,
              &next,
              CF09_A_COMMIT_NOT_COMMITTED) ==
          CF09_A_RESULT_NOT_COMMITTED);
    CHECK(cf09_a_discard_not_committed(&driver, &next) ==
          CF09_A_RESULT_OK);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_OK);
    return true;
}

static bool test_reset_abort_failure_retains_owner_until_deinit(void)
{
    cf09_a_driver_t driver;
    cf09_a_fake_backend_t fake;
    const uint8_t payload[] = {'r', 's', 't'};
    cf09_a_tx_request_t request = test_request(
        4U, 1U, 1U, payload, sizeof(payload));
    cf09_a_attempt_token_t token;

    CHECK(make_ready(&driver, &fake, 26U));
    CHECK(cf09_a_begin_tx(&driver, &request, &token) == CF09_A_RESULT_OK);
    cf09_a_fake_set_abort_succeeds(&fake, false);
    CHECK(cf09_a_on_device_reset(&driver) ==
          CF09_A_RESULT_BACKEND_FAILURE);
    CHECK(driver._abort_pending);
    CHECK(driver._host_pending);
    CHECK(fake.active);
    CHECK(!fake.outputs_enabled);
    CHECK(cf09_a_on_host_result(
              &driver,
              &token,
              CF09_A_COMMIT_COMMITTED) == CF09_A_RESULT_STALE_TOKEN);
    cf09_a_fake_set_abort_succeeds(&fake, true);
    CHECK(cf09_a_driver_deinit(&driver) == CF09_A_RESULT_UNCERTAIN);
    CHECK(!fake.active);
    return true;
}

int main(void)
{
    static const cf09_test_case_t tests[] = {
        {"routes_and_evidence_boundary", test_routes_and_evidence_boundary},
        {"bringup_requires_config_idle_and_arm",
         test_bringup_requires_config_idle_and_arm},
        {"commit_latch_and_completion_are_distinct",
         test_commit_latch_and_completion_are_distinct},
        {"single_host_bus_owner", test_single_host_bus_owner},
        {"not_committed_requires_fresh_token",
         test_not_committed_requires_fresh_token},
        {"unknown_commit_is_uncertain_without_retry",
         test_unknown_commit_is_uncertain_without_retry},
        {"three_channels_complete_in_reverse_order",
         test_three_channels_complete_in_reverse_order},
        {"status_poll_is_explicit_completion_fallback",
         test_status_poll_is_explicit_completion_fallback},
        {"timeout_quarantines_late_completion",
         test_timeout_quarantines_late_completion},
        {"device_reset_quarantines_pending_and_inflight",
         test_device_reset_quarantines_pending_and_inflight},
        {"bus_reset_invalidates_old_generation",
         test_bus_reset_invalidates_old_generation},
        {"forged_tokens_fail_closed", test_forged_tokens_fail_closed},
        {"backend_busy_and_reject_do_not_mutate_slot",
         test_backend_busy_and_reject_do_not_mutate_slot},
        {"backend_close_failure_is_fail_closed",
         test_backend_close_failure_is_fail_closed},
        {"zero_length_payload_and_null_bounds",
         test_zero_length_payload_and_null_bounds},
        {"deinit_aborts_pending_and_late_callback_is_invalid",
         test_deinit_aborts_pending_and_late_callback_is_invalid},
        {"counter_wrap_attempts_fail_closed",
         test_counter_wrap_attempts_fail_closed},
        {"generation_wrap_reset_fails_closed",
         test_generation_wrap_reset_fails_closed},
        {"unknown_backend_acceptance_disables_outputs",
         test_unknown_backend_acceptance_disables_outputs},
        {"double_init_is_rejected_without_losing_backend",
         test_double_init_is_rejected_without_losing_backend},
        {"deinit_failures_retain_recoverable_context",
         test_deinit_failures_retain_recoverable_context},
        {"backend_owned_payload_survives_caller_mutation",
         test_backend_owned_payload_survives_caller_mutation},
        {"frame_identity_must_increase_after_reap",
         test_frame_identity_must_increase_after_reap},
        {"reset_abort_failure_retains_owner_until_deinit",
         test_reset_abort_failure_retains_owner_until_deinit}
    };
    size_t index;
    size_t passed = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            (void)printf("CF09 native model test failed: %s\n", tests[index].name);
            return 1;
        }
        passed++;
    }
    (void)printf(
        "CF09 native model tests: %zu/%zu PASS "
        "(MODEL_ONLY; hardware/electrical/connector NOT_RUN)\n",
        passed,
        sizeof(tests) / sizeof(tests[0]));
    return 0;
}
