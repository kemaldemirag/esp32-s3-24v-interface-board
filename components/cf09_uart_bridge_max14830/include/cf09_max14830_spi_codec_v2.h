#ifndef CF09_MAX14830_SPI_CODEC_V2_H
#define CF09_MAX14830_SPI_CODEC_V2_H

/*
 * CF-09 class-A / MIRRORED_5 MAX14830-family candidate SPI codec.
 *
 * Evidence boundary:
 *   - Source: Analog Devices MAX14830 data sheet, Rev. 7 (2016),
 *     https://www.analog.com/media/en/technical-documentation/data-sheets/MAX14830.pdf
 *   - Erratum source: Analog Devices MAX14830 TxFIFO error FAQ,
 *     https://ez.analog.com/interface-isolation/a/documents/DO17238/max14830-txfifo-error
 *   - RevID source: Analog Devices MAX14830 History of the REVID Register,
 *     https://ez.analog.com/interface-isolation/a/documents/do17243/max14830-history-of-the-revid-register
 *   - Candidate status: PROVISIONAL; no exact orderable part, schematic,
 *     PCB, voltage-domain, power-off/backfeed or connector PASS is implied.
 *   - SPI transport obligations not represented by a byte frame: MODE0,
 *     CS held low for the complete transaction, device-ready qualification,
 *     bounded timeout, bus ownership and reset/IRQ electrical handling.
 *   - UART3 is reserved for the future RX path.  This codec deliberately
 *     builds TX FIFO frames only for CF-09 logical channels L2..L4.
 *   - RevID 0xB4 is required for direct THR refill.  ADI's MAX14830 TxFIFO
 *     FAQ states that B4 fixes the earlier-silicon simultaneous THR/TX loss.
 *     B1..B3 remain explicitly blocked by this codec.
 *   - ISR[5] means the final FIFO word has started transmitting, not that
 *     the physical character has completed.  A complete-character guard or
 *     stronger physical evidence remains mandatory before completion.
 *
 * This file replaces no existing file.  v1 was found as an all-NUL file;
 * v2 is a fail-closed recovery with a distinct name.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF09_MAX14830_CANDIDATE_STATUS \
    "PROVISIONAL_PROTOTYPE_CANDIDATE / NO_PCB_PASS"
#define CF09_MAX14830_HARDWARE_STATUS "NOT_RUN / HARDWARE_REQUIRED"
#define CF09_MAX14830_ELECTRICAL_STATUS \
    "POWER_OFF_BACKFEED_UNKNOWN / PIN_FREEZE_BLOCKED"
#define CF09_MAX14830_RX_STATUS \
    "UART3_RESERVED_ONLY / RX_PATH_NOT_IMPLEMENTED / NOT_RUN"
#define CF09_MAX14830_COMPLETION_SCOPE \
    "TFIFO_EMPTY_IS_TAIL_PENDING / FINAL_CHARACTER_GUARD_REQUIRED"

enum {
    CF09_MAX14830_UART_COUNT = 4,
    CF09_MAX14830_FIFO_CAPACITY_WORDS = 128,
    CF09_MAX14830_MAX_FRAME_BYTES = 129,
    CF09_MAX14830_LOGICAL_TX_FIRST = 2,
    CF09_MAX14830_LOGICAL_TX_LAST = 4,
    CF09_MAX14830_RESERVED_RX_UART = 3,

    CF09_MAX14830_SPI_WRITE_BIT = 0x80,
    CF09_MAX14830_SPI_UART_SHIFT = 5,
    CF09_MAX14830_SPI_REGISTER_MASK = 0x1F,

    CF09_MAX14830_REG_THR_RHR = 0x00,
    CF09_MAX14830_REG_IRQ_ENABLE = 0x01,
    CF09_MAX14830_REG_ISR = 0x02,
    CF09_MAX14830_REG_LSR = 0x04,
    CF09_MAX14830_REG_SPECIAL_CHAR_INTERRUPT = 0x06,
    CF09_MAX14830_REG_STATUS_INTERRUPT = 0x08,
    CF09_MAX14830_REG_MODE1 = 0x09,
    CF09_MAX14830_REG_MODE2 = 0x0A,
    CF09_MAX14830_REG_LCR = 0x0B,
    CF09_MAX14830_REG_FIFO_TRIGGER = 0x10,
    CF09_MAX14830_REG_TX_FIFO_LEVEL = 0x11,
    CF09_MAX14830_REG_RX_FIFO_LEVEL = 0x12,
    CF09_MAX14830_REG_PLL_CONFIG = 0x1A,
    CF09_MAX14830_REG_DIV_LSB = 0x1C,
    CF09_MAX14830_REG_CLOCK_SOURCE = 0x1E,
    CF09_MAX14830_REG_GLOBAL = 0x1F,

    CF09_MAX14830_MODE1_IRQ_SELECT_BIT = 0x80,
    CF09_MAX14830_MODE2_FIFO_RESET_BIT = 0x02,
    CF09_MAX14830_MODE2_SOFTWARE_RESET_BIT = 0x01,
    CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT = 0x20,

    CF09_MAX14830_GLOBAL_ENABLE_EXTENDED = 0xCE,
    CF09_MAX14830_GLOBAL_DISABLE_EXTENDED = 0xCD,
    CF09_MAX14830_EXTENDED_REVID_OFFSET = 0x05,
    CF09_MAX14830_REVID_REV7 = 0xB4,
    CF09_MAX14830_REVID_FAMILY_MASK = 0xF0,
    CF09_MAX14830_REVID_FAMILY_VALUE = 0xB0
};

typedef enum {
    CF09_MAX14830_RESULT_OK = 0,
    CF09_MAX14830_RESULT_INVALID_ARGUMENT,
    CF09_MAX14830_RESULT_INVALID_STATE,
    CF09_MAX14830_RESULT_BUFFER_TOO_SMALL,
    CF09_MAX14830_RESULT_FIFO_LIMIT,
    CF09_MAX14830_RESULT_UNSUPPORTED_SILICON,
    CF09_MAX14830_RESULT_NOT_READY,
    CF09_MAX14830_RESULT_PROTOCOL_ERROR
} cf09_max14830_result_t;

typedef enum {
    CF09_MAX14830_SPI_READ = 0,
    CF09_MAX14830_SPI_WRITE
} cf09_max14830_spi_direction_t;

typedef enum {
    CF09_MAX14830_REVISION_REJECTED = 0,
    CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
    CF09_MAX14830_REVISION_LEGACY_TXFIFO_ERRATUM,
    CF09_MAX14830_REVISION_FAMILY_UNVERIFIED
} cf09_max14830_revision_class_t;

typedef enum {
    CF09_MAX14830_TX_NOT_COMMITTED = 0,
    CF09_MAX14830_TX_HOST_COMMITTED,
    CF09_MAX14830_TX_FIFO_EMPTY_TAIL_PENDING,
    CF09_MAX14830_TX_SERIALIZER_DONE_GUARD_CONFIRMED,
    CF09_MAX14830_TX_UNCERTAIN
} cf09_max14830_tx_evidence_t;

typedef struct {
    uint8_t bytes[CF09_MAX14830_MAX_FRAME_BYTES];
    size_t len;
} cf09_max14830_spi_frame_t;

typedef struct {
    cf09_max14830_spi_frame_t enable_extended;
    cf09_max14830_spi_frame_t read_revid;
    cf09_max14830_spi_frame_t disable_extended;
} cf09_max14830_revision_probe_t;

typedef struct {
    bool host_committed;
    bool acceptance_unknown;
    bool tx_fifo_empty_observed;
    bool final_character_guard_confirmed;
    bool reset_since_commit;
} cf09_max14830_tx_observation_t;

cf09_max14830_result_t cf09_max14830_encode_command(
    cf09_max14830_spi_direction_t direction,
    uint8_t uart,
    uint8_t register_address,
    uint8_t *command_out);

cf09_max14830_result_t cf09_max14830_decode_command(
    uint8_t command,
    cf09_max14830_spi_direction_t *direction_out,
    uint8_t *uart_out,
    uint8_t *register_address_out);

cf09_max14830_result_t cf09_max14830_uart_for_logical_tx(
    uint8_t logical_channel,
    uint8_t *uart_out);

/* Standard-space access only; hazardous MODE2 reset writes are rejected. */
cf09_max14830_result_t cf09_max14830_build_register_write(
    uint8_t uart,
    uint8_t register_address,
    uint8_t value,
    cf09_max14830_spi_frame_t *frame_out);

/* RHR access is intentionally absent until the reserved RX path is built. */
cf09_max14830_result_t cf09_max14830_build_register_read(
    uint8_t uart,
    uint8_t register_address,
    cf09_max14830_spi_frame_t *frame_out);

/*
 * revision_class must come from a successful RevID probe on the same live
 * device and bus generation.  A reset, rebind or failed extended-mode cleanup
 * invalidates that attestation.  The future stateful backend owns this binding;
 * passing an enum literal is not hardware evidence.  payload may alias
 * frame_out because the builder snapshots it before clearing the destination.
 */
cf09_max14830_result_t cf09_max14830_build_cf09_tx_fifo_write(
    cf09_max14830_revision_class_t revision_class,
    uint8_t logical_channel,
    const uint8_t *payload,
    size_t payload_len,
    cf09_max14830_spi_frame_t *frame_out);

cf09_max14830_result_t cf09_max14830_build_global_command(
    uint8_t global_command,
    cf09_max14830_spi_frame_t *frame_out);

cf09_max14830_result_t cf09_max14830_build_revision_probe(
    cf09_max14830_revision_probe_t *probe_out);

cf09_max14830_result_t cf09_max14830_decode_single_read_data(
    const uint8_t *rx_bytes,
    size_t rx_len,
    uint8_t *value_out);

cf09_max14830_revision_class_t cf09_max14830_classify_revision(
    uint8_t revision);

bool cf09_max14830_isr_reports_tx_fifo_empty(uint8_t isr_value);

cf09_max14830_result_t cf09_max14830_classify_tx_evidence(
    const cf09_max14830_tx_observation_t *observation,
    cf09_max14830_tx_evidence_t *evidence_out);

const char *cf09_max14830_result_name(cf09_max14830_result_t result);

const char *cf09_max14830_tx_evidence_name(
    cf09_max14830_tx_evidence_t evidence);

#ifdef __cplusplus
}
#endif

#endif
