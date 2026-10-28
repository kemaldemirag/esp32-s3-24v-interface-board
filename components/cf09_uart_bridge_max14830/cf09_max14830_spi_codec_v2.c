#include "cf09_max14830_spi_codec_v2.h"

#include <string.h>

static bool cf09_max14830_direction_is_valid(
    cf09_max14830_spi_direction_t direction)
{
    return direction == CF09_MAX14830_SPI_READ ||
           direction == CF09_MAX14830_SPI_WRITE;
}
static bool cf09_max14830_uart_is_valid(uint8_t uart)
{
    return uart < (uint8_t)CF09_MAX14830_UART_COUNT;
}

static bool cf09_max14830_register_is_valid(uint8_t register_address)
{
    return register_address <= (uint8_t)CF09_MAX14830_SPI_REGISTER_MASK;
}

static bool cf09_max14830_global_command_is_supported(uint8_t command)
{
    return command == (uint8_t)CF09_MAX14830_GLOBAL_ENABLE_EXTENDED ||
           command == (uint8_t)CF09_MAX14830_GLOBAL_DISABLE_EXTENDED ||
           (command >= 0xE0U && command <= 0xEFU);
}

static bool cf09_max14830_register_write_is_supported(
    uint8_t uart,
    uint8_t register_address,
    uint8_t value)
{
    switch (register_address) {
    case CF09_MAX14830_REG_THR_RHR:
    case CF09_MAX14830_REG_ISR:
    case CF09_MAX14830_REG_LSR:
    case CF09_MAX14830_REG_SPECIAL_CHAR_INTERRUPT:
    case CF09_MAX14830_REG_STATUS_INTERRUPT:
    case CF09_MAX14830_REG_TX_FIFO_LEVEL:
    case CF09_MAX14830_REG_RX_FIFO_LEVEL:
    case CF09_MAX14830_REG_GLOBAL:
        return false;
    default:
        break;
    }
    if ((register_address == (uint8_t)CF09_MAX14830_REG_PLL_CONFIG ||
         register_address == (uint8_t)CF09_MAX14830_REG_CLOCK_SOURCE) &&
        uart != 0U) {
        return false;
    }
    if (register_address == (uint8_t)CF09_MAX14830_REG_MODE2 &&
        (value & ((uint8_t)CF09_MAX14830_MODE2_SOFTWARE_RESET_BIT |
                  (uint8_t)CF09_MAX14830_MODE2_FIFO_RESET_BIT)) != 0U) {
        return false;
    }
    return true;
}

static void cf09_max14830_clear_frame(cf09_max14830_spi_frame_t *frame)
{
    if (frame != NULL) {
        memset(frame, 0, sizeof(*frame));
    }
}

cf09_max14830_result_t cf09_max14830_encode_command(
    cf09_max14830_spi_direction_t direction,
    uint8_t uart,
    uint8_t register_address,
    uint8_t *command_out)
{
    uint8_t command;

    if (command_out == NULL || !cf09_max14830_direction_is_valid(direction) ||
        !cf09_max14830_uart_is_valid(uart) ||
        !cf09_max14830_register_is_valid(register_address)) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }

    command = (uint8_t)(uart << CF09_MAX14830_SPI_UART_SHIFT);
    command = (uint8_t)(command |
                        (register_address &
                         (uint8_t)CF09_MAX14830_SPI_REGISTER_MASK));
    if (direction == CF09_MAX14830_SPI_WRITE) {
        command = (uint8_t)(command | (uint8_t)CF09_MAX14830_SPI_WRITE_BIT);
    }
    *command_out = command;
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_decode_command(
    uint8_t command,
    cf09_max14830_spi_direction_t *direction_out,
    uint8_t *uart_out,
    uint8_t *register_address_out)
{
    if (direction_out == NULL || uart_out == NULL ||
        register_address_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }

    *direction_out =
        (command & (uint8_t)CF09_MAX14830_SPI_WRITE_BIT) != 0U
            ? CF09_MAX14830_SPI_WRITE
            : CF09_MAX14830_SPI_READ;
    *uart_out = (uint8_t)((command >> CF09_MAX14830_SPI_UART_SHIFT) & 0x03U);
    *register_address_out =
        (uint8_t)(command & (uint8_t)CF09_MAX14830_SPI_REGISTER_MASK);
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_uart_for_logical_tx(
    uint8_t logical_channel,
    uint8_t *uart_out)
{
    if (uart_out == NULL ||
        logical_channel < (uint8_t)CF09_MAX14830_LOGICAL_TX_FIRST ||
        logical_channel > (uint8_t)CF09_MAX14830_LOGICAL_TX_LAST) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }

    *uart_out =
        (uint8_t)(logical_channel - (uint8_t)CF09_MAX14830_LOGICAL_TX_FIRST);
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_build_register_write(
    uint8_t uart,
    uint8_t register_address,
    uint8_t value,
    cf09_max14830_spi_frame_t *frame_out)
{
    cf09_max14830_result_t result;

    if (frame_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    cf09_max14830_clear_frame(frame_out);
    if (!cf09_max14830_uart_is_valid(uart) ||
        !cf09_max14830_register_is_valid(register_address) ||
        !cf09_max14830_register_write_is_supported(
            uart, register_address, value)) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    result = cf09_max14830_encode_command(
        CF09_MAX14830_SPI_WRITE,
        uart,
        register_address,
        &frame_out->bytes[0]);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    frame_out->bytes[1] = value;
    frame_out->len = 2U;
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_build_register_read(
    uint8_t uart,
    uint8_t register_address,
    cf09_max14830_spi_frame_t *frame_out)
{
    cf09_max14830_result_t result;

    if (frame_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    cf09_max14830_clear_frame(frame_out);
    if (register_address == (uint8_t)CF09_MAX14830_REG_THR_RHR ||
        ((register_address ==
              (uint8_t)CF09_MAX14830_REG_PLL_CONFIG ||
          register_address ==
              (uint8_t)CF09_MAX14830_REG_CLOCK_SOURCE) &&
         uart != 0U)) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    result = cf09_max14830_encode_command(
        CF09_MAX14830_SPI_READ,
        uart,
        register_address,
        &frame_out->bytes[0]);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    frame_out->bytes[1] = 0U;
    frame_out->len = 2U;
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_build_cf09_tx_fifo_write(
    cf09_max14830_revision_class_t revision_class,
    uint8_t logical_channel,
    const uint8_t *payload,
    size_t payload_len,
    cf09_max14830_spi_frame_t *frame_out)
{
    cf09_max14830_result_t result;
    uint8_t payload_copy[CF09_MAX14830_FIFO_CAPACITY_WORDS];
    uint8_t uart;

    if (frame_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (revision_class !=
        CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4) {
        cf09_max14830_clear_frame(frame_out);
        return CF09_MAX14830_RESULT_UNSUPPORTED_SILICON;
    }
    if (payload == NULL || payload_len == 0U) {
        cf09_max14830_clear_frame(frame_out);
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (payload_len > (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS) {
        cf09_max14830_clear_frame(frame_out);
        return CF09_MAX14830_RESULT_FIFO_LIMIT;
    }
    memcpy(payload_copy, payload, payload_len);
    cf09_max14830_clear_frame(frame_out);
    result = cf09_max14830_uart_for_logical_tx(logical_channel, &uart);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    result = cf09_max14830_encode_command(
        CF09_MAX14830_SPI_WRITE,
        uart,
        (uint8_t)CF09_MAX14830_REG_THR_RHR,
        &frame_out->bytes[0]);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    memcpy(&frame_out->bytes[1], payload_copy, payload_len);
    frame_out->len = payload_len + 1U;
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_build_global_command(
    uint8_t global_command,
    cf09_max14830_spi_frame_t *frame_out)
{
    cf09_max14830_result_t result;

    if (frame_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    cf09_max14830_clear_frame(frame_out);
    if (!cf09_max14830_global_command_is_supported(global_command)) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    result = cf09_max14830_encode_command(
        CF09_MAX14830_SPI_WRITE,
        0U,
        (uint8_t)CF09_MAX14830_REG_GLOBAL,
        &frame_out->bytes[0]);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    frame_out->bytes[1] = global_command;
    frame_out->len = 2U;
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_build_revision_probe(
    cf09_max14830_revision_probe_t *probe_out)
{
    cf09_max14830_result_t result;

    if (probe_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    memset(probe_out, 0, sizeof(*probe_out));
    result = cf09_max14830_build_global_command(
        (uint8_t)CF09_MAX14830_GLOBAL_ENABLE_EXTENDED,
        &probe_out->enable_extended);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    result = cf09_max14830_build_register_read(
        0U,
        (uint8_t)CF09_MAX14830_EXTENDED_REVID_OFFSET,
        &probe_out->read_revid);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    return cf09_max14830_build_global_command(
        (uint8_t)CF09_MAX14830_GLOBAL_DISABLE_EXTENDED,
        &probe_out->disable_extended);
}

cf09_max14830_result_t cf09_max14830_decode_single_read_data(
    const uint8_t *rx_bytes,
    size_t rx_len,
    uint8_t *value_out)
{
    if (rx_bytes == NULL || value_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (rx_len < 2U) {
        return CF09_MAX14830_RESULT_BUFFER_TOO_SMALL;
    }
    *value_out = rx_bytes[1];
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_revision_class_t cf09_max14830_classify_revision(
    uint8_t revision)
{
    if (revision == (uint8_t)CF09_MAX14830_REVID_REV7) {
        return CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4;
    }
    if (revision >= 0xB1U && revision <= 0xB3U) {
        return CF09_MAX14830_REVISION_LEGACY_TXFIFO_ERRATUM;
    }
    if ((revision & (uint8_t)CF09_MAX14830_REVID_FAMILY_MASK) ==
        (uint8_t)CF09_MAX14830_REVID_FAMILY_VALUE) {
        return CF09_MAX14830_REVISION_FAMILY_UNVERIFIED;
    }
    return CF09_MAX14830_REVISION_REJECTED;
}

bool cf09_max14830_isr_reports_tx_fifo_empty(uint8_t isr_value)
{
    return (isr_value & (uint8_t)CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT) != 0U;
}

cf09_max14830_result_t cf09_max14830_classify_tx_evidence(
    const cf09_max14830_tx_observation_t *observation,
    cf09_max14830_tx_evidence_t *evidence_out)
{
    if (observation == NULL || evidence_out == NULL) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }

    if (observation->acceptance_unknown) {
        *evidence_out = CF09_MAX14830_TX_UNCERTAIN;
        return CF09_MAX14830_RESULT_OK;
    }
    if (!observation->host_committed) {
        if (observation->tx_fifo_empty_observed ||
            observation->final_character_guard_confirmed ||
            observation->reset_since_commit) {
            return CF09_MAX14830_RESULT_INVALID_STATE;
        }
        *evidence_out = CF09_MAX14830_TX_NOT_COMMITTED;
        return CF09_MAX14830_RESULT_OK;
    }
    if (observation->reset_since_commit) {
        *evidence_out = CF09_MAX14830_TX_UNCERTAIN;
        return CF09_MAX14830_RESULT_OK;
    }
    if (observation->final_character_guard_confirmed &&
        !observation->tx_fifo_empty_observed) {
        return CF09_MAX14830_RESULT_INVALID_STATE;
    }
    if (observation->final_character_guard_confirmed) {
        *evidence_out = CF09_MAX14830_TX_SERIALIZER_DONE_GUARD_CONFIRMED;
        return CF09_MAX14830_RESULT_OK;
    }
    if (observation->tx_fifo_empty_observed) {
        *evidence_out = CF09_MAX14830_TX_FIFO_EMPTY_TAIL_PENDING;
        return CF09_MAX14830_RESULT_OK;
    }
    *evidence_out = CF09_MAX14830_TX_HOST_COMMITTED;
    return CF09_MAX14830_RESULT_OK;
}

const char *cf09_max14830_result_name(cf09_max14830_result_t result)
{
    switch (result) {
    case CF09_MAX14830_RESULT_OK:
        return "OK";
    case CF09_MAX14830_RESULT_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case CF09_MAX14830_RESULT_INVALID_STATE:
        return "INVALID_STATE";
    case CF09_MAX14830_RESULT_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    case CF09_MAX14830_RESULT_FIFO_LIMIT:
        return "FIFO_LIMIT";
    case CF09_MAX14830_RESULT_UNSUPPORTED_SILICON:
        return "UNSUPPORTED_SILICON";
    case CF09_MAX14830_RESULT_NOT_READY:
        return "NOT_READY";
    case CF09_MAX14830_RESULT_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    default:
        return "INVALID_ENUM";
    }
}

const char *cf09_max14830_tx_evidence_name(
    cf09_max14830_tx_evidence_t evidence)
{
    switch (evidence) {
    case CF09_MAX14830_TX_NOT_COMMITTED:
        return "TX_NOT_COMMITTED";
    case CF09_MAX14830_TX_HOST_COMMITTED:
        return "TX_HOST_COMMITTED";
    case CF09_MAX14830_TX_FIFO_EMPTY_TAIL_PENDING:
        return "TX_FIFO_EMPTY_TAIL_PENDING";
    case CF09_MAX14830_TX_SERIALIZER_DONE_GUARD_CONFIRMED:
        return "TX_SERIALIZER_DONE_GUARD_CONFIRMED";
    case CF09_MAX14830_TX_UNCERTAIN:
        return "TX_UNCERTAIN";
    default:
        return "TX_INVALID_ENUM";
    }
}
