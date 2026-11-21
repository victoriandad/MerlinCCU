#include "psram.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

namespace psram
{

namespace
{

#if defined(PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN)
constexpr int kChipSelectPin = PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN;
#else
constexpr int kChipSelectPin = -1;
#endif

/// Uncached, non-allocating window onto chip select 1. Used by the self-test
/// so aliasing and pattern checks see the chip, not the XIP cache.
constexpr uintptr_t kUncachedBase = 0x15000000U;
/// The XIP M1 window is 16 MB, the largest chip it can address.
constexpr uint32_t kWindowBytes = 16U * 1024U * 1024U;
constexpr uint32_t kMiB = 1024U * 1024U;

// APS6404L command set (datasheet section 9).
constexpr uint8_t kCmdExitQuad = 0xF5U;
constexpr uint8_t kCmdReadId = 0x9FU;
constexpr uint8_t kCmdResetEnable = 0x66U;
constexpr uint8_t kCmdReset = 0x99U;
constexpr uint8_t kCmdEnterQuad = 0x35U;
constexpr uint8_t kCmdQuadFastRead = 0xEBU;
constexpr uint8_t kCmdQuadWrite = 0x38U;
constexpr uint8_t kKnownGoodDie = 0x5DU;
// Read ID returns the command echo and 24 address bits, then KGD and EID.
constexpr int kReadIdKgdByte = 5;
constexpr int kReadIdEidByte = 6;
constexpr int kReadIdLength = 7;

// Direct-mode serial clock while probing: clk_sys / 10, well under the
// part's 33 MHz limit for the slow 0x9F read.
constexpr uint32_t kDirectModeClockDivider = 10U;
// Datasheet timing: 133 MHz maximum clock in quad mode, CE# low for at most
// 8 us (tCEM, so refresh can run), CE# high for at least 18 ns (tCPH).
constexpr uint32_t kMaxClockHz = 133000000U;
constexpr uint32_t kRxDelayExtraAboveHz = 100000000U;
constexpr uint64_t kFemtosecondsPerSecond = 1000000000000000ULL;
// tCEM (8000 ns) in the timing register's units of 64 clocks, scaled to fs.
constexpr uint64_t kMaxSelectFs = 125ULL * 1000000ULL;
constexpr uint64_t kMinDeselectFs = 18ULL * 1000000ULL;

constexpr uint32_t kSpeedTestBytes = 256U * 1024U;
constexpr uint32_t kPatternStrideBytes = 1024U;

void __no_inline_not_in_flash_func(wait_while_busy)()
{
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0U)
    {
    }
}

/// @brief Sends one single-byte command with chip select pulsed around it.
void __no_inline_not_in_flash_func(send_command)(uint8_t command, bool quad)
{
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    uint32_t tx = command;
    if (quad)
    {
        tx |= QMI_DIRECT_TX_OE_BITS | (QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB);
    }
    qmi_hw->direct_tx = tx;
    wait_while_busy();
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_AUTO_CS1N_BITS;
}

/// @brief Probes and configures the chip. Must not touch flash: it runs
/// with the QMI in direct mode, when XIP from flash is unavailable.
Info __no_inline_not_in_flash_func(setup_in_ram)(uint32_t sys_hz)
{
    Info info = {};

    qmi_hw->direct_csr = (kDirectModeClockDivider << QMI_DIRECT_CSR_CLKDIV_LSB) |
                         QMI_DIRECT_CSR_EN_BITS;
    wait_while_busy();

    // The chip may still be in quad mode from before a soft reset.
    send_command(kCmdExitQuad, true);

    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    for (int i = 0; i < kReadIdLength; ++i)
    {
        qmi_hw->direct_tx = (i == 0) ? kCmdReadId : 0xFFU;
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0U)
        {
        }
        wait_while_busy();
        const auto byte = static_cast<uint8_t>(qmi_hw->direct_rx);
        if (i == kReadIdKgdByte)
        {
            info.kgd = byte;
        }
        else if (i == kReadIdEidByte)
        {
            info.eid = byte;
        }
    }
    qmi_hw->direct_csr &= ~QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    if (info.kgd != kKnownGoodDie)
    {
        qmi_hw->direct_csr = 0U;
        return info;
    }

    send_command(kCmdResetEnable, false);
    send_command(kCmdReset, false);
    send_command(kCmdEnterQuad, false);
    qmi_hw->direct_csr = 0U;

    uint32_t divisor = (sys_hz + kMaxClockHz - 1U) / kMaxClockHz;
    // RP2350 datasheet: a divisor of 1 is only reliable at low clock speeds.
    if (divisor == 1U && sys_hz > kRxDelayExtraAboveHz)
    {
        divisor = 2U;
    }
    uint32_t rx_delay = divisor;
    if (sys_hz / divisor > kRxDelayExtraAboveHz)
    {
        ++rx_delay;
    }
    const uint64_t clock_period_fs = kFemtosecondsPerSecond / sys_hz;
    const auto max_select = static_cast<uint32_t>(kMaxSelectFs / clock_period_fs);
    const auto min_deselect = static_cast<uint32_t>(
        (kMinDeselectFs + clock_period_fs - 1U) / clock_period_fs - (divisor + 1U) / 2U);

    qmi_hw->m[1].timing = (1U << QMI_M1_TIMING_COOLDOWN_LSB) |
                          (QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB) |
                          (max_select << QMI_M1_TIMING_MAX_SELECT_LSB) |
                          (min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB) |
                          (rx_delay << QMI_M1_TIMING_RXDELAY_LSB) |
                          (divisor << QMI_M1_TIMING_CLKDIV_LSB);
    qmi_hw->m[1].rfmt = (QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB) |
                        (QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB) |
                        (QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB) |
                        (QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB) |
                        (QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB) |
                        (QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB) |
                        (QMI_M1_RFMT_DUMMY_LEN_VALUE_24 << QMI_M1_RFMT_DUMMY_LEN_LSB);
    qmi_hw->m[1].rcmd = kCmdQuadFastRead;
    qmi_hw->m[1].wfmt = (QMI_M1_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_PREFIX_WIDTH_LSB) |
                        (QMI_M1_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M1_RFMT_ADDR_WIDTH_LSB) |
                        (QMI_M1_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M1_RFMT_SUFFIX_WIDTH_LSB) |
                        (QMI_M1_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M1_RFMT_DUMMY_WIDTH_LSB) |
                        (QMI_M1_RFMT_DATA_WIDTH_VALUE_Q << QMI_M1_RFMT_DATA_WIDTH_LSB) |
                        (QMI_M1_RFMT_PREFIX_LEN_VALUE_8 << QMI_M1_RFMT_PREFIX_LEN_LSB);
    qmi_hw->m[1].wcmd = kCmdQuadWrite;

    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
    info.present = true;
    return info;
}

/// @brief Decodes the density from the EID byte (APS6404L: top three bits).
uint32_t size_from_eid(uint8_t eid)
{
    constexpr uint8_t kDensityShift = 5U;
    constexpr uint8_t kEid8MiB = 0x26U;
    switch (eid >> kDensityShift)
    {
    case 0U:
        return 2U * kMiB;
    case 1U:
        return 4U * kMiB;
    case 2U:
        return 8U * kMiB;
    default:
        return eid == kEid8MiB ? 8U * kMiB : kMiB;
    }
}

volatile uint32_t* uncached_word(uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(kUncachedBase + offset);
}

volatile uint32_t* cached_word(uint32_t offset)
{
    return reinterpret_cast<volatile uint32_t*>(kCachedBase + offset);
}

/// @brief Value written at `offset` by the pattern test: unlike its neighbours
/// and its aliases, so a stuck or shorted address line shows up.
uint32_t pattern_for(uint32_t offset)
{
    constexpr uint32_t kPatternMix = 0x9E3779B9U;
    return (offset * kPatternMix) ^ offset;
}

/// @brief Kilobytes per second for `bytes` moved in `elapsed_us`.
uint32_t kib_per_second(uint32_t bytes, uint64_t elapsed_us)
{
    constexpr uint64_t kMicrosecondsPerSecond = 1000000ULL;
    constexpr uint64_t kBytesPerKib = 1024ULL;
    return elapsed_us == 0U
               ? 0U
               : static_cast<uint32_t>((bytes * kMicrosecondsPerSecond) /
                                       (elapsed_us * kBytesPerKib));
}

} // namespace

Info init()
{
    if (kChipSelectPin < 0)
    {
        return {};
    }

    gpio_set_function(static_cast<uint>(kChipSelectPin), GPIO_FUNC_XIP_CS1);
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint32_t interrupts = save_and_disable_interrupts();
    Info info = setup_in_ram(sys_hz);
    restore_interrupts(interrupts);

    if (info.present)
    {
        info.size_bytes = size_from_eid(info.eid);
    }
    return info;
}

SelfTest self_test(const Info& info)
{
    SelfTest result = {};
    if (!info.present)
    {
        return result;
    }

    // Size: a write at a power-of-two offset past the end of the chip lands
    // on offset 0, because the high address bits are not decoded.
    constexpr uint32_t kMarkerAtZero = 0xA5A5A5A5U;
    *uncached_word(0U) = kMarkerAtZero;
    uint32_t verified = kWindowBytes;
    for (uint32_t offset = kMiB; offset < kWindowBytes; offset <<= 1U)
    {
        *uncached_word(offset) = ~kMarkerAtZero;
        if (*uncached_word(0U) != kMarkerAtZero)
        {
            verified = offset;
            break;
        }
    }
    result.verified_bytes = verified;

    // Pattern: write every stride first, then read back, so an alias that
    // overwrites an earlier word is caught.
    for (uint32_t offset = 0U; offset < verified; offset += kPatternStrideBytes)
    {
        *uncached_word(offset) = pattern_for(offset);
    }
    result.passed = true;
    for (uint32_t offset = 0U; offset < verified; offset += kPatternStrideBytes)
    {
        if (*uncached_word(offset) != pattern_for(offset))
        {
            result.passed = false;
            result.first_bad_address = kUncachedBase + offset;
            break;
        }
    }

    // Speed through the cache, as ordinary code will use it. The block is
    // far larger than the 16 KB XIP cache, so this measures the chip.
    uint64_t start = time_us_64();
    for (uint32_t offset = 0U; offset < kSpeedTestBytes; offset += sizeof(uint32_t))
    {
        *cached_word(offset) = offset;
    }
    result.write_kib_per_s = kib_per_second(kSpeedTestBytes, time_us_64() - start);

    uint32_t checksum = 0U;
    start = time_us_64();
    for (uint32_t offset = 0U; offset < kSpeedTestBytes; offset += sizeof(uint32_t))
    {
        checksum += *cached_word(offset);
    }
    result.read_kib_per_s = kib_per_second(kSpeedTestBytes, time_us_64() - start);
    (void)checksum;

    return result;
}

} // namespace psram
