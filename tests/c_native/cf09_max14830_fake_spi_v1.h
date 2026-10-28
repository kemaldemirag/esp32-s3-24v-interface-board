#ifndef CF09_MAX14830_FAKE_SPI_V1_H
#define CF09_MAX14830_FAKE_SPI_V1_H

#include "cf09_max14830_spi_codec_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool initialized;
    bool reset_active;
    bool ready;
    bool extended_registers_enabled;
    bool final_character_active[CF09_MAX14830_UART_COUNT];
    bool final_character_complete[CF09_MAX14830_UART_COUNT];
    uint8_t revision_id;
    uint8_t registers[CF09_MAX14830_UART_COUNT][32];
    uint8_t tx_fifo[CF09_MAX14830_UART_COUNT]
                   [CF09_MAX14830_FIFO_CAPACITY_WORDS];
    size_t tx_fifo_len[CF09_MAX14830_UART_COUNT];
    uint64_t transfer_count;
    uint64_t global_trigger_count;
    uint64_t reset_generation;
} cf09_max14830_fake_spi_t;

void cf09_max14830_fake_spi_init(cf09_max14830_fake_spi_t *fake);

cf09_max14830_result_t cf09_max14830_fake_assert_reset(
    cf09_max14830_fake_spi_t *fake);

cf09_max14830_result_t cf09_max14830_fake_release_reset(
    cf09_max14830_fake_spi_t *fake);

cf09_max14830_result_t cf09_max14830_fake_mark_ready(
    cf09_max14830_fake_spi_t *fake);

void cf09_max14830_fake_set_revision(
    cf09_max14830_fake_spi_t *fake,
    uint8_t revision_id);

/*
 * rx_bytes mirrors a full-duplex SPI transaction.  For a two-byte read the
 * low nibble of rx_bytes[0] is the fast-read IRQ sample and rx_bytes[1] is
 * the selected register value.  Writes return rx_len_out == 0.
 */
cf09_max14830_result_t cf09_max14830_fake_transfer(
    cf09_max14830_fake_spi_t *fake,
    const cf09_max14830_spi_frame_t *frame,
    uint8_t *rx_bytes,
    size_t rx_capacity,
    size_t *rx_len_out);

/* Advances simulated time until the last FIFO word starts serializing. */
cf09_max14830_result_t
cf09_max14830_fake_advance_to_final_character_start(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart);

/* Supplies the external one-complete-character guard/physical completion. */
cf09_max14830_result_t cf09_max14830_fake_finish_final_character(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart);

size_t cf09_max14830_fake_tx_fifo_level(
    const cf09_max14830_fake_spi_t *fake,
    uint8_t uart);

bool cf09_max14830_fake_tx_fifo_byte(
    const cf09_max14830_fake_spi_t *fake,
    uint8_t uart,
    size_t index,
    uint8_t *value_out);

#endif
