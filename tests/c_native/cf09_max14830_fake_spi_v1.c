#include "cf09_max14830_fake_spi_v1.h"

#include <string.h>

static bool cf09_max14830_fake_uart_valid(uint8_t uart)
{
    return uart < (uint8_t)CF09_MAX14830_UART_COUNT;
}

static bool cf09_max14830_fake_register_read_only(uint8_t register_address)
{
    switch (register_address) {
    case CF09_MAX14830_REG_ISR:
    case CF09_MAX14830_REG_LSR:
    case CF09_MAX14830_REG_SPECIAL_CHAR_INTERRUPT:
    case CF09_MAX14830_REG_STATUS_INTERRUPT:
    case CF09_MAX14830_REG_TX_FIFO_LEVEL:
    case CF09_MAX14830_REG_RX_FIFO_LEVEL:
        return true;
    default:
        return false;
    }
}

static void cf09_max14830_fake_reset_runtime(
    cf09_max14830_fake_spi_t *fake)
{
    uint8_t uart;

    memset(fake->registers, 0, sizeof(fake->registers));
    memset(fake->tx_fifo, 0, sizeof(fake->tx_fifo));
    memset(fake->tx_fifo_len, 0, sizeof(fake->tx_fifo_len));
    memset(fake->final_character_active, 0,
           sizeof(fake->final_character_active));
    memset(fake->final_character_complete, 0,
           sizeof(fake->final_character_complete));
    fake->extended_registers_enabled = false;
    for (uart = 0U; uart < (uint8_t)CF09_MAX14830_UART_COUNT; ++uart) {
        /* MAX14830 Rev.7 register-map reset values, datasheet pp. 28-29. */
        fake->registers[uart][CF09_MAX14830_REG_ISR] = 0x60U;
        fake->registers[uart][CF09_MAX14830_REG_LCR] = 0x05U;
        fake->registers[uart][CF09_MAX14830_REG_FIFO_TRIGGER] = 0xFFU;
        fake->registers[uart][CF09_MAX14830_REG_PLL_CONFIG] = 0x01U;
        fake->registers[uart][CF09_MAX14830_REG_DIV_LSB] = 0x01U;
        fake->registers[uart][CF09_MAX14830_REG_CLOCK_SOURCE] = 0x08U;
    }
}

static uint8_t cf09_max14830_fake_global_irq_mask(
    const cf09_max14830_fake_spi_t *fake)
{
    uint8_t mask = 0x0FU;
    uint8_t uart;

    /* The Linux max310x behavior reference treats cleared bits as pending. */
    for (uart = 0U; uart < (uint8_t)CF09_MAX14830_UART_COUNT; ++uart) {
        uint8_t enabled_pending =
            (uint8_t)(fake->registers[uart][CF09_MAX14830_REG_ISR] &
                      fake->registers[uart]
                                     [CF09_MAX14830_REG_IRQ_ENABLE]);
        bool normal_irq_selected =
            (fake->registers[uart][CF09_MAX14830_REG_MODE1] &
             (uint8_t)CF09_MAX14830_MODE1_IRQ_SELECT_BIT) != 0U;

        if (normal_irq_selected && enabled_pending != 0U) {
            mask = (uint8_t)(mask & (uint8_t)~(uint8_t)(1U << uart));
        }
    }
    return mask;
}

static cf09_max14830_result_t cf09_max14830_fake_read_register(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart,
    uint8_t register_address,
    uint8_t *value_out)
{
    if (fake->extended_registers_enabled) {
        if (register_address !=
            (uint8_t)CF09_MAX14830_EXTENDED_REVID_OFFSET) {
            return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
        }
        *value_out = fake->revision_id;
        return CF09_MAX14830_RESULT_OK;
    }

    if (register_address == (uint8_t)CF09_MAX14830_REG_GLOBAL) {
        /* Reading GlobalIRQ must not clear a per-UART ISR. */
        *value_out = cf09_max14830_fake_global_irq_mask(fake);
        return CF09_MAX14830_RESULT_OK;
    }
    if (register_address == (uint8_t)CF09_MAX14830_REG_TX_FIFO_LEVEL) {
        *value_out = (uint8_t)fake->tx_fifo_len[uart];
        return CF09_MAX14830_RESULT_OK;
    }
    *value_out = fake->registers[uart][register_address];
    if (register_address == (uint8_t)CF09_MAX14830_REG_ISR) {
        /* ISR is clear-on-read; GlobalIRQ is explicitly not. */
        fake->registers[uart][register_address] = 0U;
    }
    return CF09_MAX14830_RESULT_OK;
}

static cf09_max14830_result_t cf09_max14830_fake_write_global(
    cf09_max14830_fake_spi_t *fake,
    const cf09_max14830_spi_frame_t *frame)
{
    uint8_t command;

    if (frame->len != 2U) {
        return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
    }
    command = frame->bytes[1];
    if (command == (uint8_t)CF09_MAX14830_GLOBAL_ENABLE_EXTENDED) {
        fake->extended_registers_enabled = true;
    } else if (command ==
               (uint8_t)CF09_MAX14830_GLOBAL_DISABLE_EXTENDED) {
        fake->extended_registers_enabled = false;
    } else if (command >= 0xE0U && command <= 0xEFU) {
        /* A trigger starts already-configured UARTs; it copies no payload. */
        fake->global_trigger_count++;
    } else {
        return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
    }
    return CF09_MAX14830_RESULT_OK;
}

static cf09_max14830_result_t cf09_max14830_fake_write_fifo(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart,
    const cf09_max14830_spi_frame_t *frame)
{
    size_t payload_len = frame->len - 1U;
    size_t current_len = fake->tx_fifo_len[uart];

    if (payload_len == 0U) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (payload_len >
        (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS - current_len) {
        return CF09_MAX14830_RESULT_FIFO_LIMIT;
    }
    memcpy(&fake->tx_fifo[uart][current_len], &frame->bytes[1], payload_len);
    fake->tx_fifo_len[uart] = current_len + payload_len;
    fake->final_character_active[uart] = false;
    fake->final_character_complete[uart] = false;
    return CF09_MAX14830_RESULT_OK;
}

static cf09_max14830_result_t cf09_max14830_fake_write_registers(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart,
    uint8_t first_register,
    const cf09_max14830_spi_frame_t *frame)
{
    size_t index;

    for (index = 1U; index < frame->len; ++index) {
        size_t register_value = (size_t)first_register + index - 1U;
        if (register_value >
                (size_t)CF09_MAX14830_SPI_REGISTER_MASK ||
            register_value == (size_t)CF09_MAX14830_REG_GLOBAL ||
            cf09_max14830_fake_register_read_only((uint8_t)register_value)) {
            return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
        }
        if (((register_value ==
                  (size_t)CF09_MAX14830_REG_PLL_CONFIG ||
              register_value ==
                  (size_t)CF09_MAX14830_REG_CLOCK_SOURCE) &&
             uart != 0U) ||
            (register_value == (size_t)CF09_MAX14830_REG_MODE2 &&
             (frame->bytes[index] &
              ((uint8_t)CF09_MAX14830_MODE2_SOFTWARE_RESET_BIT |
               (uint8_t)CF09_MAX14830_MODE2_FIFO_RESET_BIT)) != 0U)) {
            return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
        }
    }
    for (index = 1U; index < frame->len; ++index) {
        size_t register_value = (size_t)first_register + index - 1U;
        fake->registers[uart][register_value] = frame->bytes[index];
    }
    return CF09_MAX14830_RESULT_OK;
}

void cf09_max14830_fake_spi_init(cf09_max14830_fake_spi_t *fake)
{
    if (fake == NULL) {
        return;
    }
    memset(fake, 0, sizeof(*fake));
    fake->initialized = true;
    fake->reset_active = true;
    fake->revision_id = (uint8_t)CF09_MAX14830_REVID_REV7;
    fake->reset_generation = 1U;
    cf09_max14830_fake_reset_runtime(fake);
}

cf09_max14830_result_t cf09_max14830_fake_assert_reset(
    cf09_max14830_fake_spi_t *fake)
{
    if (fake == NULL || !fake->initialized) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (fake->reset_active || fake->reset_generation == UINT64_MAX) {
        return CF09_MAX14830_RESULT_INVALID_STATE;
    }
    fake->reset_generation++;
    fake->reset_active = true;
    fake->ready = false;
    cf09_max14830_fake_reset_runtime(fake);
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_fake_release_reset(
    cf09_max14830_fake_spi_t *fake)
{
    if (fake == NULL || !fake->initialized) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (!fake->reset_active) {
        return CF09_MAX14830_RESULT_INVALID_STATE;
    }
    cf09_max14830_fake_reset_runtime(fake);
    fake->reset_active = false;
    fake->ready = false;
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_fake_mark_ready(
    cf09_max14830_fake_spi_t *fake)
{
    if (fake == NULL || !fake->initialized) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (fake->reset_active) {
        return CF09_MAX14830_RESULT_INVALID_STATE;
    }
    fake->ready = true;
    return CF09_MAX14830_RESULT_OK;
}

void cf09_max14830_fake_set_revision(
    cf09_max14830_fake_spi_t *fake,
    uint8_t revision_id)
{
    if (fake != NULL && fake->initialized) {
        fake->revision_id = revision_id;
    }
}

cf09_max14830_result_t cf09_max14830_fake_transfer(
    cf09_max14830_fake_spi_t *fake,
    const cf09_max14830_spi_frame_t *frame,
    uint8_t *rx_bytes,
    size_t rx_capacity,
    size_t *rx_len_out)
{
    cf09_max14830_spi_direction_t direction;
    cf09_max14830_result_t result;
    uint8_t uart;
    uint8_t register_address;
    uint8_t value;
    uint8_t fast_irq_sample;

    if (fake == NULL || frame == NULL || rx_len_out == NULL ||
        !fake->initialized || frame->len == 0U ||
        frame->len > (size_t)CF09_MAX14830_MAX_FRAME_BYTES) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    *rx_len_out = 0U;
    if (!fake->ready || fake->reset_active) {
        return CF09_MAX14830_RESULT_NOT_READY;
    }
    result = cf09_max14830_decode_command(
        frame->bytes[0],
        &direction,
        &uart,
        &register_address);
    if (result != CF09_MAX14830_RESULT_OK ||
        !cf09_max14830_fake_uart_valid(uart)) {
        return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
    }

    if (direction == CF09_MAX14830_SPI_READ) {
        if (frame->len != 2U) {
            return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
        }
        if (rx_bytes == NULL || rx_capacity < 2U) {
            return CF09_MAX14830_RESULT_BUFFER_TOO_SMALL;
        }
        fast_irq_sample = cf09_max14830_fake_global_irq_mask(fake);
        result = cf09_max14830_fake_read_register(
            fake, uart, register_address, &value);
        if (result != CF09_MAX14830_RESULT_OK) {
            return result;
        }
        rx_bytes[0] = (uint8_t)(0xF0U | fast_irq_sample);
        rx_bytes[1] = value;
        *rx_len_out = 2U;
        fake->transfer_count++;
        return CF09_MAX14830_RESULT_OK;
    }

    if (frame->len < 2U) {
        return CF09_MAX14830_RESULT_PROTOCOL_ERROR;
    }
    if (register_address == (uint8_t)CF09_MAX14830_REG_GLOBAL) {
        result = cf09_max14830_fake_write_global(fake, frame);
    } else if (register_address ==
               (uint8_t)CF09_MAX14830_REG_THR_RHR) {
        result = cf09_max14830_fake_write_fifo(fake, uart, frame);
    } else {
        result = cf09_max14830_fake_write_registers(
            fake, uart, register_address, frame);
    }
    if (result == CF09_MAX14830_RESULT_OK) {
        fake->transfer_count++;
    }
    return result;
}

cf09_max14830_result_t
cf09_max14830_fake_advance_to_final_character_start(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart)
{
    if (fake == NULL || !fake->initialized ||
        !cf09_max14830_fake_uart_valid(uart)) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (!fake->ready || fake->reset_active) {
        return CF09_MAX14830_RESULT_NOT_READY;
    }
    if (fake->tx_fifo_len[uart] == 0U ||
        fake->final_character_active[uart]) {
        return CF09_MAX14830_RESULT_INVALID_STATE;
    }
    fake->tx_fifo_len[uart] = 0U;
    fake->final_character_active[uart] = true;
    fake->final_character_complete[uart] = false;
    fake->registers[uart][CF09_MAX14830_REG_ISR] =
        (uint8_t)(fake->registers[uart][CF09_MAX14830_REG_ISR] |
                  (uint8_t)CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT);
    return CF09_MAX14830_RESULT_OK;
}

cf09_max14830_result_t cf09_max14830_fake_finish_final_character(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart)
{
    if (fake == NULL || !fake->initialized ||
        !cf09_max14830_fake_uart_valid(uart)) {
        return CF09_MAX14830_RESULT_INVALID_ARGUMENT;
    }
    if (!fake->ready || fake->reset_active) {
        return CF09_MAX14830_RESULT_NOT_READY;
    }
    if (!fake->final_character_active[uart] ||
        fake->final_character_complete[uart]) {
        return CF09_MAX14830_RESULT_INVALID_STATE;
    }
    fake->final_character_complete[uart] = true;
    return CF09_MAX14830_RESULT_OK;
}

size_t cf09_max14830_fake_tx_fifo_level(
    const cf09_max14830_fake_spi_t *fake,
    uint8_t uart)
{
    if (fake == NULL || !fake->initialized ||
        !cf09_max14830_fake_uart_valid(uart)) {
        return 0U;
    }
    return fake->tx_fifo_len[uart];
}

bool cf09_max14830_fake_tx_fifo_byte(
    const cf09_max14830_fake_spi_t *fake,
    uint8_t uart,
    size_t index,
    uint8_t *value_out)
{
    if (fake == NULL || value_out == NULL || !fake->initialized ||
        !cf09_max14830_fake_uart_valid(uart) ||
        index >= fake->tx_fifo_len[uart]) {
        return false;
    }
    *value_out = fake->tx_fifo[uart][index];
    return true;
}
