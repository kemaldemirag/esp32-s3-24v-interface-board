#include "cf09_max14830_spi_codec_v2.h"
#include "cf09_max14830_fake_spi_v2.h"

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

typedef bool (*cf09_max14830_test_fn)(void);

typedef struct {
    const char *name;
    cf09_max14830_test_fn run;
} cf09_max14830_test_case_t;

static bool make_fake_ready(cf09_max14830_fake_spi_t *fake)
{
    cf09_max14830_fake_spi_init(fake);
    CHECK(cf09_max14830_fake_release_reset(fake) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_mark_ready(fake) ==
          CF09_MAX14830_RESULT_OK);
    return true;
}
static cf09_max14830_result_t read_register(
    cf09_max14830_fake_spi_t *fake,
    uint8_t uart,
    uint8_t register_address,
    uint8_t *value_out)
{
    cf09_max14830_spi_frame_t frame;
    cf09_max14830_result_t result;
    uint8_t rx[2];
    size_t rx_len = 0U;

    result = cf09_max14830_build_register_read(
        uart, register_address, &frame);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    result = cf09_max14830_fake_transfer(
        fake, &frame, rx, sizeof(rx), &rx_len);
    if (result != CF09_MAX14830_RESULT_OK) {
        return result;
    }
    return cf09_max14830_decode_single_read_data(rx, rx_len, value_out);
}

static bool test_evidence_boundary_and_route_mapping(void)
{
    uint8_t uart = UINT8_MAX;

    CHECK(strcmp(CF09_MAX14830_CANDIDATE_STATUS,
                 "PROVISIONAL_PROTOTYPE_CANDIDATE / NO_PCB_PASS") == 0);
    CHECK(strcmp(CF09_MAX14830_HARDWARE_STATUS,
                 "NOT_RUN / HARDWARE_REQUIRED") == 0);
    CHECK(strcmp(CF09_MAX14830_ELECTRICAL_STATUS,
                 "POWER_OFF_BACKFEED_UNKNOWN / PIN_FREEZE_BLOCKED") == 0);
    CHECK(strcmp(CF09_MAX14830_RX_STATUS,
                 "UART3_RESERVED_ONLY / RX_PATH_NOT_IMPLEMENTED / NOT_RUN") ==
          0);
    CHECK(strcmp(CF09_MAX14830_COMPLETION_SCOPE,
                 "TFIFO_EMPTY_IS_TAIL_PENDING / "
                 "FINAL_CHARACTER_GUARD_REQUIRED") == 0);
    CHECK(cf09_max14830_uart_for_logical_tx(2U, &uart) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(uart == 0U);
    CHECK(cf09_max14830_uart_for_logical_tx(3U, &uart) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(uart == 1U);
    CHECK(cf09_max14830_uart_for_logical_tx(4U, &uart) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(uart == 2U);
    CHECK((uint8_t)CF09_MAX14830_RESERVED_RX_UART == 3U);
    return true;
}

static bool test_spi_command_golden_vectors(void)
{
    uint8_t command = 0U;

    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_WRITE, 0U, 0U, &command) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(command == 0x80U);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_WRITE, 1U, 0U, &command) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(command == 0xA0U);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_WRITE, 2U, 0U, &command) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(command == 0xC0U);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_WRITE, 3U, 0U, &command) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(command == 0xE0U);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_READ,
              2U,
              (uint8_t)CF09_MAX14830_REG_ISR,
              &command) == CF09_MAX14830_RESULT_OK);
    CHECK(command == 0x42U);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_WRITE,
              0U,
              (uint8_t)CF09_MAX14830_REG_GLOBAL,
              &command) == CF09_MAX14830_RESULT_OK);
    CHECK(command == 0x9FU);
    return true;
}

static bool test_command_roundtrip_all_fields(void)
{
    uint8_t direction_value;

    for (direction_value = 0U; direction_value < 2U; ++direction_value) {
        uint8_t uart;
        for (uart = 0U; uart < (uint8_t)CF09_MAX14830_UART_COUNT; ++uart) {
            uint8_t register_address;
            for (register_address = 0U;
                 register_address <=
                     (uint8_t)CF09_MAX14830_SPI_REGISTER_MASK;
                 ++register_address) {
                cf09_max14830_spi_direction_t direction =
                    direction_value == 0U ? CF09_MAX14830_SPI_READ
                                          : CF09_MAX14830_SPI_WRITE;
                cf09_max14830_spi_direction_t decoded_direction;
                uint8_t decoded_uart = UINT8_MAX;
                uint8_t decoded_register = UINT8_MAX;
                uint8_t command = 0U;

                CHECK(cf09_max14830_encode_command(
                          direction,
                          uart,
                          register_address,
                          &command) == CF09_MAX14830_RESULT_OK);
                CHECK(cf09_max14830_decode_command(
                          command,
                          &decoded_direction,
                          &decoded_uart,
                          &decoded_register) == CF09_MAX14830_RESULT_OK);
                CHECK(decoded_direction == direction);
                CHECK(decoded_uart == uart);
                CHECK(decoded_register == register_address);
            }
        }
    }
    return true;
}

static bool test_invalid_command_and_route_arguments(void)
{
    cf09_max14830_spi_frame_t frame;
    uint8_t command = 0U;
    uint8_t uart = 0U;

    CHECK(cf09_max14830_encode_command(
              (cf09_max14830_spi_direction_t)99, 0U, 0U, &command) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_READ, 4U, 0U, &command) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_READ, 0U, 0x20U, &command) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_encode_command(
              CF09_MAX14830_SPI_READ, 0U, 0U, NULL) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_decode_command(0U, NULL, &uart, &command) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_uart_for_logical_tx(1U, &uart) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_uart_for_logical_tx(5U, &uart) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(cf09_max14830_uart_for_logical_tx(2U, NULL) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_register_write(
              3U,
              (uint8_t)CF09_MAX14830_REG_THR_RHR,
              0x55U,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_register_read(
              3U,
              (uint8_t)CF09_MAX14830_REG_THR_RHR,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    return true;
}

static bool test_cf09_fifo_frames_are_no_increment_and_reserve_uart3(void)
{
    const uint8_t payload[] = {0x11U, 0x22U, 0x33U};
    cf09_max14830_spi_frame_t frame;
    uint8_t oversized[CF09_MAX14830_FIFO_CAPACITY_WORDS + 1U];

    memset(oversized, 0x5AU, sizeof(oversized));
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(frame.len == 4U);
    CHECK(frame.bytes[0] == 0x80U);
    CHECK(memcmp(&frame.bytes[1], payload, sizeof(payload)) == 0);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              3U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(frame.bytes[0] == 0xA0U);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              4U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(frame.bytes[0] == 0xC0U);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              5U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, oversized, sizeof(oversized), &frame) ==
          CF09_MAX14830_RESULT_FIFO_LIMIT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, 0U, &frame) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    frame.bytes[1] = 0x91U;
    frame.bytes[2] = 0x92U;
    frame.bytes[3] = 0x93U;
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, &frame.bytes[1], 3U, &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(frame.len == 4U && frame.bytes[0] == 0x80U);
    CHECK(frame.bytes[1] == 0x91U && frame.bytes[2] == 0x92U &&
          frame.bytes[3] == 0x93U);
    return true;
}

static bool test_revision_probe_golden_vectors(void)
{
    cf09_max14830_revision_probe_t probe;

    CHECK(cf09_max14830_build_revision_probe(&probe) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(probe.enable_extended.len == 2U);
    CHECK(probe.enable_extended.bytes[0] == 0x9FU);
    CHECK(probe.enable_extended.bytes[1] == 0xCEU);
    CHECK(probe.read_revid.len == 2U);
    CHECK(probe.read_revid.bytes[0] == 0x05U);
    CHECK(probe.read_revid.bytes[1] == 0x00U);
    CHECK(probe.disable_extended.len == 2U);
    CHECK(probe.disable_extended.bytes[0] == 0x9FU);
    CHECK(probe.disable_extended.bytes[1] == 0xCDU);
    CHECK(cf09_max14830_build_revision_probe(NULL) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    return true;
}

static bool test_revision_classes_fail_closed(void)
{
    uint8_t revision;

    CHECK(cf09_max14830_classify_revision(0xB4U) ==
          CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4);
    for (revision = 0xB1U; revision <= 0xB3U; ++revision) {
        CHECK(cf09_max14830_classify_revision(revision) ==
              CF09_MAX14830_REVISION_LEGACY_TXFIFO_ERRATUM);
    }
    CHECK(cf09_max14830_classify_revision(0xB0U) ==
          CF09_MAX14830_REVISION_FAMILY_UNVERIFIED);
    CHECK(cf09_max14830_classify_revision(0xB5U) ==
          CF09_MAX14830_REVISION_FAMILY_UNVERIFIED);
    CHECK(cf09_max14830_classify_revision(0xB7U) ==
          CF09_MAX14830_REVISION_FAMILY_UNVERIFIED);
    CHECK(cf09_max14830_classify_revision(0xA0U) ==
          CF09_MAX14830_REVISION_REJECTED);
    CHECK(cf09_max14830_classify_revision(0xC0U) ==
          CF09_MAX14830_REVISION_REJECTED);
    return true;
}

static bool test_fake_requires_reset_release_and_ready(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    uint8_t rx[2];
    size_t rx_len = 0U;

    cf09_max14830_fake_spi_init(&fake);
    CHECK(cf09_max14830_build_register_read(
              0U,
              (uint8_t)CF09_MAX14830_REG_DIV_LSB,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_NOT_READY);
    CHECK(cf09_max14830_fake_mark_ready(&fake) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    CHECK(cf09_max14830_fake_release_reset(&fake) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_NOT_READY);
    CHECK(cf09_max14830_fake_mark_ready(&fake) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    return true;
}

static bool test_fake_reset_defaults_and_clear_on_read_isr(void)
{
    cf09_max14830_fake_spi_t fake;
    uint8_t value = 0U;

    CHECK(make_fake_ready(&fake));
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_DIV_LSB, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(value == 0x01U);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_LCR, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(value == 0x05U);
    CHECK(read_register(
              &fake,
              0U,
              (uint8_t)CF09_MAX14830_REG_FIFO_TRIGGER,
              &value) == CF09_MAX14830_RESULT_OK);
    CHECK(value == 0xFFU);
    CHECK(read_register(
              &fake,
              0U,
              (uint8_t)CF09_MAX14830_REG_CLOCK_SOURCE,
              &value) == CF09_MAX14830_RESULT_OK);
    CHECK(value == 0x08U);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_ISR, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(value == 0x60U);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_ISR, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(value == 0x00U);
    return true;
}

static bool test_fake_revision_probe_and_extended_exit(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_revision_probe_t probe;
    uint8_t rx[2];
    uint8_t revision = 0U;
    size_t rx_len = 0U;

    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_revision_probe(&probe) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake,
              &probe.enable_extended,
              NULL,
              0U,
              &rx_len) == CF09_MAX14830_RESULT_OK);
    CHECK(fake.extended_registers_enabled);
    CHECK(cf09_max14830_fake_transfer(
              &fake,
              &probe.read_revid,
              rx,
              sizeof(rx),
              &rx_len) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_decode_single_read_data(
              rx, rx_len, &revision) == CF09_MAX14830_RESULT_OK);
    CHECK(revision == (uint8_t)CF09_MAX14830_REVID_REV7);
    CHECK(cf09_max14830_classify_revision(revision) ==
          CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4);
    CHECK(cf09_max14830_fake_transfer(
              &fake,
              &probe.disable_extended,
              NULL,
              0U,
              &rx_len) == CF09_MAX14830_RESULT_OK);
    CHECK(!fake.extended_registers_enabled);
    return true;
}

static bool test_fifo_write_stays_same_address(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    const uint8_t payload[] = {0xA5U, 0x5AU, 0xC3U, 0x3CU};
    uint8_t isr = 0U;
    size_t rx_len = 0U;
    size_t index;

    CHECK(make_fake_ready(&fake));
    CHECK(read_register(
              &fake, 1U, (uint8_t)CF09_MAX14830_REG_ISR, &isr) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              3U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 1U) == sizeof(payload));
    for (index = 0U; index < sizeof(payload); ++index) {
        uint8_t value = 0U;
        CHECK(cf09_max14830_fake_tx_fifo_byte(
            &fake, 1U, index, &value));
        CHECK(value == payload[index]);
    }
    CHECK(fake.registers[1U][CF09_MAX14830_REG_IRQ_ENABLE] == 0U);
    CHECK(fake.registers[1U][CF09_MAX14830_REG_ISR] == 0x00U);
    return true;
}

static bool test_fifo_capacity_and_overflow_are_atomic(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    cf09_max14830_spi_frame_t extra;
    uint8_t payload[CF09_MAX14830_FIFO_CAPACITY_WORDS];
    size_t rx_len = 0U;

    memset(payload, 0x69, sizeof(payload));
    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 0U) ==
          (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, 1U, &extra) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &extra, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_FIFO_LIMIT);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 0U) ==
          (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS);
    return true;
}

static bool test_read_only_register_write_is_rejected(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    size_t rx_len = 0U;

    CHECK(make_fake_ready(&fake));
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_register_write(
              0U,
              (uint8_t)CF09_MAX14830_REG_ISR,
              0U,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    frame.bytes[0] = 0x82U;
    frame.bytes[1] = 0x00U;
    frame.len = 2U;
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_PROTOCOL_ERROR);
    CHECK(fake.transfer_count == 0U);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_register_write(
              0U,
              (uint8_t)CF09_MAX14830_REG_MODE2,
              (uint8_t)CF09_MAX14830_MODE2_SOFTWARE_RESET_BIT,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_global_command(0x7FU, &frame) ==
          CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_register_write(
              1U,
              (uint8_t)CF09_MAX14830_REG_PLL_CONFIG,
              0x01U,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U);
    frame.bytes[0] = 0x8AU;
    frame.bytes[1] =
        (uint8_t)CF09_MAX14830_MODE2_FIFO_RESET_BIT;
    frame.len = 2U;
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_PROTOCOL_ERROR);
    CHECK(fake.transfer_count == 0U);
    return true;
}

static bool test_irq_requires_enable_and_irq_selection(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    uint8_t value = 0U;
    size_t rx_len = 0U;

    CHECK(make_fake_ready(&fake));
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_GLOBAL, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK((value & 0x0FU) == 0x0FU);
    CHECK(cf09_max14830_build_register_write(
              0U,
              (uint8_t)CF09_MAX14830_REG_IRQ_ENABLE,
              (uint8_t)CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_GLOBAL, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK((value & 0x01U) == 0x01U);
    CHECK(cf09_max14830_build_register_write(
              0U,
              (uint8_t)CF09_MAX14830_REG_MODE1,
              (uint8_t)CF09_MAX14830_MODE1_IRQ_SELECT_BIT,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_GLOBAL, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK((value & 0x01U) == 0U);
    return true;
}

static bool test_global_irq_read_does_not_clear_per_uart_isr(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    const uint8_t payload[] = {0x7EU};
    uint8_t value = 0U;
    size_t rx_len = 0U;

    CHECK(make_fake_ready(&fake));
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_ISR, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_build_register_write(
              0U,
              (uint8_t)CF09_MAX14830_REG_IRQ_ENABLE,
              (uint8_t)CF09_MAX14830_ISR_TX_FIFO_EMPTY_BIT,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_build_register_write(
              0U,
              (uint8_t)CF09_MAX14830_REG_MODE1,
              (uint8_t)CF09_MAX14830_MODE1_IRQ_SELECT_BIT,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_advance_to_final_character_start(&fake, 0U) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_GLOBAL, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK((value & 0x01U) == 0U);
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_ISR, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_isr_reports_tx_fifo_empty(value));
    CHECK(read_register(
              &fake, 0U, (uint8_t)CF09_MAX14830_REG_ISR, &value) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(!cf09_max14830_isr_reports_tx_fifo_empty(value));
    return true;
}

static bool test_fifo_empty_is_tail_pending_not_complete(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    cf09_max14830_spi_frame_t refill;
    cf09_max14830_tx_observation_t observation;
    cf09_max14830_tx_evidence_t evidence;
    const uint8_t payload[] = {0x10U, 0x20U};
    uint8_t isr = 0U;
    size_t rx_len = 0U;

    CHECK(make_fake_ready(&fake));
    CHECK(read_register(
              &fake, 2U, (uint8_t)CF09_MAX14830_REG_ISR, &isr) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              4U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_advance_to_final_character_start(&fake, 2U) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(read_register(
              &fake, 2U, (uint8_t)CF09_MAX14830_REG_ISR, &isr) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_isr_reports_tx_fifo_empty(isr));

    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              4U, payload, sizeof(payload), &refill) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &refill, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    CHECK(fake.final_character_active[2U]);
    CHECK(!fake.final_character_complete[2U]);

    memset(&observation, 0, sizeof(observation));
    observation.host_committed = true;
    observation.tx_fifo_empty_observed = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(evidence == CF09_MAX14830_TX_FIFO_EMPTY_TAIL_PENDING);
    CHECK(!fake.final_character_complete[2U]);

    CHECK(cf09_max14830_fake_finish_final_character(&fake, 2U) ==
          CF09_MAX14830_RESULT_OK);
    observation.final_character_guard_confirmed = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(evidence ==
          CF09_MAX14830_TX_SERIALIZER_DONE_GUARD_CONFIRMED);
    CHECK(fake.final_character_complete[2U]);
    return true;
}

static bool test_completion_state_machine_rejects_impossible_order(void)
{
    cf09_max14830_tx_observation_t observation;
    cf09_max14830_tx_evidence_t evidence;

    memset(&observation, 0, sizeof(observation));
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(evidence == CF09_MAX14830_TX_NOT_COMMITTED);
    observation.host_committed = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(evidence == CF09_MAX14830_TX_HOST_COMMITTED);
    observation.final_character_guard_confirmed = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    observation.host_committed = false;
    observation.tx_fifo_empty_observed = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    return true;
}

static bool test_unknown_acceptance_and_reset_are_uncertain(void)
{
    cf09_max14830_tx_observation_t observation;
    cf09_max14830_tx_evidence_t evidence;

    memset(&observation, 0, sizeof(observation));
    observation.acceptance_unknown = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(evidence == CF09_MAX14830_TX_UNCERTAIN);
    memset(&observation, 0, sizeof(observation));
    observation.host_committed = true;
    observation.reset_since_commit = true;
    CHECK(cf09_max14830_classify_tx_evidence(&observation, &evidence) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(evidence == CF09_MAX14830_TX_UNCERTAIN);
    return true;
}

static bool test_reset_invalidates_runtime_and_requires_reprobe(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    const uint8_t payload[] = {0xD0U, 0x0DU};
    uint8_t rx[2];
    size_t rx_len = 0U;
    uint64_t generation;

    CHECK(make_fake_ready(&fake));
    generation = fake.reset_generation;
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_assert_reset(&fake) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(fake.reset_generation == generation + 1U);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 0U) == 0U);
    CHECK(cf09_max14830_build_register_read(
              0U,
              (uint8_t)CF09_MAX14830_REG_DIV_LSB,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_NOT_READY);
    CHECK(cf09_max14830_fake_release_reset(&fake) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_mark_ready(&fake) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    return true;
}

static bool test_global_trigger_is_control_only(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    const uint8_t payload[] = {0x01U, 0x02U, 0x03U};
    size_t rx_len = 0U;
    size_t before;

    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U, payload, sizeof(payload), &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    before = cf09_max14830_fake_tx_fifo_level(&fake, 0U);
    CHECK(cf09_max14830_build_global_command(0xE8U, &frame) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(fake.global_trigger_count == 1U);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 0U) == before);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 1U) == 0U);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 2U) == 0U);
    return true;
}

static bool test_legacy_and_unverified_silicon_are_blocked(void)
{
    const uint8_t payload[] = {0x31U, 0x32U};
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    size_t rx_len = 99U;

    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_LEGACY_TXFIFO_ERRATUM,
              2U,
              payload,
              sizeof(payload),
              &frame) == CF09_MAX14830_RESULT_UNSUPPORTED_SILICON);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_FAMILY_UNVERIFIED,
              2U,
              payload,
              sizeof(payload),
              &frame) == CF09_MAX14830_RESULT_UNSUPPORTED_SILICON);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_REJECTED,
              2U,
              payload,
              sizeof(payload),
              &frame) == CF09_MAX14830_RESULT_UNSUPPORTED_SILICON);

    CHECK(make_fake_ready(&fake));
    cf09_max14830_fake_set_revision(&fake, 0xB3U);
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U,
              payload,
              sizeof(payload),
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_UNSUPPORTED_SILICON);
    CHECK(rx_len == 0U);
    CHECK(fake.transfer_count == 0U);
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 0U) == 0U);
    return true;
}

static bool test_global_clock_register_access_is_uart0_only(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    uint8_t rx[2] = {0U, 0U};
    size_t rx_len = 99U;

    CHECK(cf09_max14830_build_register_read(
              0U,
              (uint8_t)CF09_MAX14830_REG_PLL_CONFIG,
              &frame) == CF09_MAX14830_RESULT_OK);
    memset(&frame, 0xA5, sizeof(frame));
    CHECK(cf09_max14830_build_register_read(
              1U,
              (uint8_t)CF09_MAX14830_REG_PLL_CONFIG,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);
    CHECK(frame.len == 0U && frame.bytes[0] == 0U);
    CHECK(cf09_max14830_build_register_read(
              3U,
              (uint8_t)CF09_MAX14830_REG_CLOCK_SOURCE,
              &frame) == CF09_MAX14830_RESULT_INVALID_ARGUMENT);

    CHECK(make_fake_ready(&fake));
    frame.bytes[0] = 0x3AU; /* read, UART1, PLLConfig(0x1A) */
    frame.bytes[1] = 0U;
    frame.len = 2U;
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_PROTOCOL_ERROR);
    CHECK(rx_len == 0U);
    CHECK(fake.transfer_count == 0U);
    return true;
}

static bool test_corrupt_fifo_and_counter_exhaustion_fail_closed(void)
{
    const uint8_t payload[] = {0x5AU};
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    uint8_t rx[2] = {0U, 0U};
    size_t rx_len = 99U;

    CHECK(make_fake_ready(&fake));
    fake.tx_fifo_len[0U] =
        (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS + 1U;
    CHECK(cf09_max14830_fake_tx_fifo_level(&fake, 0U) == 0U);
    CHECK(cf09_max14830_fake_advance_to_final_character_start(
              &fake, 0U) == CF09_MAX14830_RESULT_INVALID_STATE);
    CHECK(fake.tx_fifo_len[0U] ==
          (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS + 1U);
    {
        uint8_t value = 0U;
        CHECK(!cf09_max14830_fake_tx_fifo_byte(
            &fake, 0U, 0U, &value));
        CHECK(!cf09_max14830_fake_tx_fifo_byte(
            &fake,
            0U,
            (size_t)CF09_MAX14830_FIFO_CAPACITY_WORDS,
            &value));
        CHECK(cf09_max14830_build_register_read(
                  0U,
                  (uint8_t)CF09_MAX14830_REG_TX_FIFO_LEVEL,
                  &frame) == CF09_MAX14830_RESULT_OK);
        CHECK(cf09_max14830_fake_transfer(
                  &fake, &frame, rx, sizeof(rx), &rx_len) ==
              CF09_MAX14830_RESULT_INVALID_STATE);
        CHECK(rx_len == 0U);
    }
    CHECK(cf09_max14830_build_cf09_tx_fifo_write(
              CF09_MAX14830_REVISION_SUPPORTED_FIXED_B4,
              2U,
              payload,
              sizeof(payload),
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    CHECK(fake.transfer_count == 0U);

    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_register_read(
              0U,
              (uint8_t)CF09_MAX14830_REG_DIV_LSB,
              &frame) == CF09_MAX14830_RESULT_OK);
    fake.transfer_count = UINT64_MAX;
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    CHECK(rx_len == 0U);
    CHECK(fake.transfer_count == UINT64_MAX);

    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_global_command(0xE0U, &frame) ==
          CF09_MAX14830_RESULT_OK);
    fake.global_trigger_count = UINT64_MAX;
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, NULL, 0U, &rx_len) ==
          CF09_MAX14830_RESULT_INVALID_STATE);
    CHECK(fake.global_trigger_count == UINT64_MAX);
    CHECK(fake.transfer_count == 0U);
    return true;
}

static bool test_extended_mode_error_requires_explicit_cleanup(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_revision_probe_t probe;
    cf09_max14830_spi_frame_t bad_read;
    uint8_t rx[2] = {0U, 0U};
    size_t rx_len = 99U;

    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_revision_probe(&probe) ==
          CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake,
              &probe.enable_extended,
              NULL,
              0U,
              &rx_len) == CF09_MAX14830_RESULT_OK);
    CHECK(fake.extended_registers_enabled);
    CHECK(cf09_max14830_build_register_read(
              0U,
              (uint8_t)CF09_MAX14830_REG_SPECIAL_CHAR_INTERRUPT,
              &bad_read) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &bad_read, rx, sizeof(rx), &rx_len) ==
          CF09_MAX14830_RESULT_PROTOCOL_ERROR);
    CHECK(fake.extended_registers_enabled);
    CHECK(cf09_max14830_fake_transfer(
              &fake,
              &probe.disable_extended,
              NULL,
              0U,
              &rx_len) == CF09_MAX14830_RESULT_OK);
    CHECK(!fake.extended_registers_enabled);
    CHECK(fake.transfer_count == 2U);
    return true;
}

static bool test_buffer_bounds_and_names(void)
{
    cf09_max14830_fake_spi_t fake;
    cf09_max14830_spi_frame_t frame;
    uint8_t rx[2];
    uint8_t value = 0U;
    size_t rx_len = 0U;

    CHECK(make_fake_ready(&fake));
    CHECK(cf09_max14830_build_register_read(
              0U,
              (uint8_t)CF09_MAX14830_REG_DIV_LSB,
              &frame) == CF09_MAX14830_RESULT_OK);
    CHECK(cf09_max14830_fake_transfer(
              &fake, &frame, rx, 1U, &rx_len) ==
          CF09_MAX14830_RESULT_BUFFER_TOO_SMALL);
    CHECK(cf09_max14830_decode_single_read_data(rx, 1U, &value) ==
          CF09_MAX14830_RESULT_BUFFER_TOO_SMALL);
    CHECK(strcmp(cf09_max14830_result_name(
                     CF09_MAX14830_RESULT_FIFO_LIMIT),
                 "FIFO_LIMIT") == 0);
    CHECK(strcmp(cf09_max14830_result_name(
                     CF09_MAX14830_RESULT_UNSUPPORTED_SILICON),
                 "UNSUPPORTED_SILICON") == 0);
    CHECK(strcmp(cf09_max14830_result_name(
                     (cf09_max14830_result_t)99),
                 "INVALID_ENUM") == 0);
    CHECK(strcmp(cf09_max14830_tx_evidence_name(
                     CF09_MAX14830_TX_FIFO_EMPTY_TAIL_PENDING),
                 "TX_FIFO_EMPTY_TAIL_PENDING") == 0);
    CHECK(strcmp(cf09_max14830_tx_evidence_name(
                     (cf09_max14830_tx_evidence_t)99),
                 "TX_INVALID_ENUM") == 0);
    return true;
}

int main(void)
{
    static const cf09_max14830_test_case_t tests[] = {
        {"evidence_boundary_and_route_mapping",
         test_evidence_boundary_and_route_mapping},
        {"spi_command_golden_vectors", test_spi_command_golden_vectors},
        {"command_roundtrip_all_fields", test_command_roundtrip_all_fields},
        {"invalid_command_and_route_arguments",
         test_invalid_command_and_route_arguments},
        {"cf09_fifo_frames_are_no_increment_and_reserve_uart3",
         test_cf09_fifo_frames_are_no_increment_and_reserve_uart3},
        {"revision_probe_golden_vectors", test_revision_probe_golden_vectors},
        {"revision_classes_fail_closed",
         test_revision_classes_fail_closed},
        {"fake_requires_reset_release_and_ready",
         test_fake_requires_reset_release_and_ready},
        {"fake_reset_defaults_and_clear_on_read_isr",
         test_fake_reset_defaults_and_clear_on_read_isr},
        {"fake_revision_probe_and_extended_exit",
         test_fake_revision_probe_and_extended_exit},
        {"fifo_write_stays_same_address", test_fifo_write_stays_same_address},
        {"fifo_capacity_and_overflow_are_atomic",
         test_fifo_capacity_and_overflow_are_atomic},
        {"read_only_register_write_is_rejected",
         test_read_only_register_write_is_rejected},
        {"irq_requires_enable_and_irq_selection",
         test_irq_requires_enable_and_irq_selection},
        {"global_irq_read_does_not_clear_per_uart_isr",
         test_global_irq_read_does_not_clear_per_uart_isr},
        {"fifo_empty_is_tail_pending_not_complete",
         test_fifo_empty_is_tail_pending_not_complete},
        {"completion_state_machine_rejects_impossible_order",
         test_completion_state_machine_rejects_impossible_order},
        {"unknown_acceptance_and_reset_are_uncertain",
         test_unknown_acceptance_and_reset_are_uncertain},
        {"reset_invalidates_runtime_and_requires_reprobe",
         test_reset_invalidates_runtime_and_requires_reprobe},
        {"global_trigger_is_control_only",
         test_global_trigger_is_control_only},
        {"legacy_and_unverified_silicon_are_blocked",
         test_legacy_and_unverified_silicon_are_blocked},
        {"global_clock_register_access_is_uart0_only",
         test_global_clock_register_access_is_uart0_only},
        {"corrupt_fifo_and_counter_exhaustion_fail_closed",
         test_corrupt_fifo_and_counter_exhaustion_fail_closed},
        {"extended_mode_error_requires_explicit_cleanup",
         test_extended_mode_error_requires_explicit_cleanup},
        {"buffer_bounds_and_names", test_buffer_bounds_and_names}
    };
    size_t index;
    size_t passed = 0U;

    for (index = 0U; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            (void)printf("CF09 MAX14830 codec test failed: %s\n",
                         tests[index].name);
            return 1;
        }
        passed++;
    }
    (void)printf(
        "CF09 MAX14830 codec tests: %zu/%zu PASS "
        "(MODEL_ONLY; native compile PASS; hardware/electrical NOT_RUN)\n",
        passed,
        sizeof(tests) / sizeof(tests[0]));
    return 0;
}
