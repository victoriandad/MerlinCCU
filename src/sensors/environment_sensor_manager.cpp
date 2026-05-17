#include "environment_sensor_manager.h"

#include <algorithm>
#include <cstdio>

#include "bme_environmental_sensor.h"
#include "hardware/i2c.h"
#include "i2c_bus.h"
#include "pico/error.h"
#include "pico/stdlib.h"
#include "sgp40_air_quality_sensor.h"

#if __has_include("environment_sensor_config.h")
#include "environment_sensor_config.h"
#else
inline constexpr bool ENVIRONMENT_SENSOR_ENABLED = false;
inline constexpr int ENVIRONMENT_SENSOR_I2C_BUS = 0;
inline constexpr int ENVIRONMENT_SENSOR_I2C_SDA_GPIO = 20;
inline constexpr int ENVIRONMENT_SENSOR_I2C_SCL_GPIO = 21;
inline constexpr uint32_t ENVIRONMENT_SENSOR_I2C_BAUDRATE_HZ = 100000U;
#endif

namespace environment_sensor_manager
{
namespace
{

struct ExpectedDevice
{
    EnvironmentSensorDevice device;
    uint8_t address;
    const char* label;
};

inline constexpr uint8_t kBmeI2cAddress = 0x76U;
inline constexpr uint8_t kSgp40I2cAddress = 0x59U;

constexpr std::array<ExpectedDevice, kEnvironmentSensorDeviceCount> kWaveshareDevices = {{
    {EnvironmentSensorDevice::Tsl2591, 0x29U, "TSL2591"},
    {EnvironmentSensorDevice::Bme280OrBme680, kBmeI2cAddress, "BME280/BME680"},
    {EnvironmentSensorDevice::Icm20948, 0x68U, "ICM20948"},
    {EnvironmentSensorDevice::Ltr390, 0x53U, "LTR390"},
    {EnvironmentSensorDevice::Sgp40, kSgp40I2cAddress, "SGP40"},
}};

inline constexpr uint32_t kInitialScanDelayMs = 10000U;
inline constexpr uint32_t kDiscoveryScanIntervalMs = 30000U;
inline constexpr uint32_t kLocalConditionSampleIntervalMs = 10000U;
inline constexpr uint32_t kLocalConditionHistoryBucketIntervalMs = 5U * 60U * 1000U;
inline constexpr uint32_t kProbeTimeoutUs = 1000U;
inline constexpr uint32_t kSensorRegisterTimeoutUs = 5000U;
inline constexpr uint32_t kSgp40RegisterTimeoutUs = 10000U;
inline constexpr uint16_t kAirQualityRawScoreSpan = 3000U;
inline constexpr uint8_t kAirQualityStableMinScore = 95U;
inline constexpr uint8_t kAirQualitySlightRiseMinScore = 85U;
inline constexpr uint8_t kAirQualityVocRiseMinScore = 70U;
inline constexpr uint8_t kAirQualityHighVocMinScore = 50U;

sensors::I2cBus g_bus;
sensors::BmeEnvironmentalSensor g_bme_sensor;
sensors::Sgp40AirQualitySensor g_sgp40_sensor;
EnvironmentSensorStatus g_status{};
absolute_time_t g_next_scan_time{};
absolute_time_t g_next_local_condition_sample_time{};
absolute_time_t g_next_local_condition_history_bucket_time{};
bool g_scan_requested = false;
bool g_initialised = false;
uint16_t g_bme_bucket_sample_count = 0U;
int64_t g_bme_temperature_bucket_sum = 0;
uint64_t g_bme_pressure_bucket_sum_pa = 0U;
uint64_t g_bme_humidity_bucket_sum_milli_percent = 0U;
uint16_t g_air_quality_bucket_sample_count = 0U;
uint32_t g_air_quality_score_bucket_sum = 0U;

/// @brief Returns the Pico SDK I2C instance selected by local configuration.
i2c_inst_t* selected_i2c_instance()
{
    if (ENVIRONMENT_SENSOR_I2C_BUS == 0)
    {
        return i2c0;
    }
    if (ENVIRONMENT_SENSOR_I2C_BUS == 1)
    {
        return i2c1;
    }
    return nullptr;
}

/// @brief Clears in-progress five-minute average buckets without touching history.
void reset_history_buckets()
{
    g_bme_bucket_sample_count = 0U;
    g_bme_temperature_bucket_sum = 0;
    g_bme_pressure_bucket_sum_pa = 0U;
    g_bme_humidity_bucket_sum_milli_percent = 0U;
    g_air_quality_bucket_sample_count = 0U;
    g_air_quality_score_bucket_sum = 0U;
}

/// @brief Rebuilds the static parts of the public discovery snapshot.
void reset_status()
{
    g_status = {};
    g_status.enabled = ENVIRONMENT_SENSOR_ENABLED;
    g_status.board = ENVIRONMENT_SENSOR_ENABLED
                         ? EnvironmentSensorBoard::WavesharePicoEnvironmentSensor
                         : EnvironmentSensorBoard::None;
    g_status.health = ENVIRONMENT_SENSOR_ENABLED ? EnvironmentSensorHealth::BusReady
                                                 : EnvironmentSensorHealth::Disabled;
    g_status.i2c_bus = static_cast<int8_t>(ENVIRONMENT_SENSOR_I2C_BUS);
    g_status.sda_gpio = static_cast<int8_t>(ENVIRONMENT_SENSOR_I2C_SDA_GPIO);
    g_status.scl_gpio = static_cast<int8_t>(ENVIRONMENT_SENSOR_I2C_SCL_GPIO);
    g_status.baudrate_hz = ENVIRONMENT_SENSOR_I2C_BAUDRATE_HZ;
    g_status.bme_variant = EnvironmentBmeVariant::NotChecked;
    g_status.bme_chip_id = 0U;
    g_status.bme_last_error = PICO_ERROR_NONE;
    g_status.bme_reading_valid = false;
    g_status.bme_temperature_centi_celsius = 0;
    g_status.bme_pressure_pa = 0U;
    g_status.bme_humidity_milli_percent = 0U;
    g_status.bme_last_read_ms = 0U;
    g_status.bme_read_error = PICO_ERROR_NONE;
    g_status.bme_history_count = 0U;
    g_status.bme_temperature_history_centi_celsius.fill(0);
    g_status.bme_pressure_history_deci_hpa.fill(0U);
    g_status.bme_humidity_history_centi_percent.fill(0U);
    g_status.air_quality_raw_valid = false;
    g_status.air_quality_raw_signal = 0U;
    g_status.air_quality_baseline_raw_signal = 0U;
    g_status.air_quality_score_valid = false;
    g_status.air_quality_score = 0U;
    g_status.air_quality_last_read_ms = 0U;
    g_status.air_quality_read_error = PICO_ERROR_NONE;
    g_status.air_quality_history_count = 0U;
    g_status.air_quality_history_score.fill(0U);
    reset_history_buckets();

    for (std::size_t index = 0; index < kWaveshareDevices.size(); ++index)
    {
        g_status.devices[index] = {
            .device = kWaveshareDevices[index].device,
            .i2c_address = kWaveshareDevices[index].address,
            .detected = false,
        };
    }
}

/// @brief Compares only fields that should matter to consumers of `update()`.
bool visible_state_matches(const EnvironmentSensorStatus& lhs, const EnvironmentSensorStatus& rhs)
{
    if (lhs.enabled != rhs.enabled || lhs.board != rhs.board || lhs.health != rhs.health ||
        lhs.detected_device_count != rhs.detected_device_count || lhs.last_error != rhs.last_error)
    {
        return false;
    }

    if (lhs.bme_variant != rhs.bme_variant || lhs.bme_chip_id != rhs.bme_chip_id ||
        lhs.bme_last_error != rhs.bme_last_error ||
        lhs.bme_reading_valid != rhs.bme_reading_valid ||
        lhs.bme_temperature_centi_celsius != rhs.bme_temperature_centi_celsius ||
        lhs.bme_pressure_pa != rhs.bme_pressure_pa ||
        lhs.bme_humidity_milli_percent != rhs.bme_humidity_milli_percent ||
        lhs.bme_last_read_ms != rhs.bme_last_read_ms ||
        lhs.bme_read_error != rhs.bme_read_error ||
        lhs.bme_history_count != rhs.bme_history_count ||
        lhs.bme_temperature_history_centi_celsius != rhs.bme_temperature_history_centi_celsius ||
        lhs.bme_pressure_history_deci_hpa != rhs.bme_pressure_history_deci_hpa ||
        lhs.bme_humidity_history_centi_percent != rhs.bme_humidity_history_centi_percent ||
        lhs.air_quality_raw_valid != rhs.air_quality_raw_valid ||
        lhs.air_quality_raw_signal != rhs.air_quality_raw_signal ||
        lhs.air_quality_baseline_raw_signal != rhs.air_quality_baseline_raw_signal ||
        lhs.air_quality_score_valid != rhs.air_quality_score_valid ||
        lhs.air_quality_score != rhs.air_quality_score ||
        lhs.air_quality_last_read_ms != rhs.air_quality_last_read_ms ||
        lhs.air_quality_read_error != rhs.air_quality_read_error ||
        lhs.air_quality_history_count != rhs.air_quality_history_count ||
        lhs.air_quality_history_score != rhs.air_quality_history_score)
    {
        return false;
    }

    for (std::size_t index = 0; index < lhs.devices.size(); ++index)
    {
        if (lhs.devices[index].detected != rhs.devices[index].detected)
        {
            return false;
        }
    }

    return true;
}

/// @brief Converts the concrete BME probe result into the public manager status enum.
EnvironmentBmeVariant bme_variant_from_status(
    const sensors::BmeEnvironmentalSensorStatus& bme_status)
{
    switch (bme_status.state)
    {
    case sensors::BmeEnvironmentalSensorState::Unprobed:
        return EnvironmentBmeVariant::NotChecked;
    case sensors::BmeEnvironmentalSensorState::Missing:
        return EnvironmentBmeVariant::Missing;
    case sensors::BmeEnvironmentalSensorState::Present:
        if (bme_status.kind == sensors::BmeEnvironmentalSensorKind::Bme280)
        {
            return EnvironmentBmeVariant::Bme280;
        }
        if (bme_status.kind == sensors::BmeEnvironmentalSensorKind::Bme680)
        {
            return EnvironmentBmeVariant::Bme680;
        }
        return EnvironmentBmeVariant::Unknown;
    case sensors::BmeEnvironmentalSensorState::Unsupported:
        return EnvironmentBmeVariant::Unknown;
    case sensors::BmeEnvironmentalSensorState::Fault:
        return EnvironmentBmeVariant::Fault;
    }

    return EnvironmentBmeVariant::Unknown;
}

/// @brief Converts pascals to the compact tenths-of-hPa history unit.
uint16_t pressure_pa_to_deci_hpa(uint32_t pressure_pa)
{
    return static_cast<uint16_t>(std::min<uint32_t>((pressure_pa + 5U) / 10U, 65535U));
}

/// @brief Converts milli-percent relative humidity to the compact centi-percent unit.
uint16_t humidity_milli_percent_to_centi_percent(uint32_t humidity_milli_percent)
{
    return static_cast<uint16_t>(std::min<uint32_t>((humidity_milli_percent + 5U) / 10U, 10000U));
}

/// @brief Appends one five-minute BME280 average to the compact rolling history arrays.
void append_bme_history(EnvironmentSensorStatus& status, int16_t temperature_centi_celsius,
                        uint16_t pressure_deci_hpa, uint16_t humidity_centi_percent)
{

    if (status.bme_history_count < kLocalConditionHistoryPointCount)
    {
        const std::size_t index = status.bme_history_count;
        status.bme_temperature_history_centi_celsius[index] = temperature_centi_celsius;
        status.bme_pressure_history_deci_hpa[index] = pressure_deci_hpa;
        status.bme_humidity_history_centi_percent[index] = humidity_centi_percent;
        ++status.bme_history_count;
        return;
    }

    for (std::size_t index = 1U; index < kLocalConditionHistoryPointCount; ++index)
    {
        status.bme_temperature_history_centi_celsius[index - 1U] =
            status.bme_temperature_history_centi_celsius[index];
        status.bme_pressure_history_deci_hpa[index - 1U] =
            status.bme_pressure_history_deci_hpa[index];
        status.bme_humidity_history_centi_percent[index - 1U] =
            status.bme_humidity_history_centi_percent[index];
    }

    const std::size_t last_index = kLocalConditionHistoryPointCount - 1U;
    status.bme_temperature_history_centi_celsius[last_index] = temperature_centi_celsius;
    status.bme_pressure_history_deci_hpa[last_index] = pressure_deci_hpa;
    status.bme_humidity_history_centi_percent[last_index] = humidity_centi_percent;
}

/// @brief Converts the raw VOC signal into a local 0-100 air-quality score.
/// @details The SGP40 SRAW value is not an absolute air-quality unit. Higher raw
/// values indicate cleaner local air, so the best raw value observed locally is
/// treated as the current clean-air reference and drops from that reference are
/// mapped onto a simple operator-facing score.
uint8_t air_quality_score_from_raw(uint16_t raw_signal, uint16_t baseline_raw_signal)
{
    if (baseline_raw_signal == 0U || raw_signal >= baseline_raw_signal)
    {
        return 100U;
    }

    const uint16_t drop = static_cast<uint16_t>(baseline_raw_signal - raw_signal);
    if (drop >= kAirQualityRawScoreSpan)
    {
        return 0U;
    }

    return static_cast<uint8_t>(100U - ((static_cast<uint32_t>(drop) * 100U) /
                                        kAirQualityRawScoreSpan));
}

/// @brief Adds one valid BME280 reading to the in-progress five-minute average.
void accumulate_bme_history_sample(const sensors::BmeEnvironmentalReading& reading)
{
    if (!reading.valid)
    {
        return;
    }

    ++g_bme_bucket_sample_count;
    g_bme_temperature_bucket_sum += reading.temperature_centi_celsius;
    g_bme_pressure_bucket_sum_pa += reading.pressure_pa;
    g_bme_humidity_bucket_sum_milli_percent += reading.humidity_milli_percent;
}

/// @brief Appends one five-minute average SGP40 VOC-change score to graph history.
void append_air_quality_history(EnvironmentSensorStatus& status, uint8_t score)
{
    if (status.air_quality_history_count < kLocalConditionHistoryPointCount)
    {
        status.air_quality_history_score[status.air_quality_history_count] = score;
        ++status.air_quality_history_count;
        return;
    }

    for (std::size_t index = 1U; index < kLocalConditionHistoryPointCount; ++index)
    {
        status.air_quality_history_score[index - 1U] = status.air_quality_history_score[index];
    }

    status.air_quality_history_score[kLocalConditionHistoryPointCount - 1U] = score;
}

/// @brief Adds one valid SGP40-derived score to the in-progress five-minute average.
void accumulate_air_quality_history_sample(uint8_t score)
{
    ++g_air_quality_bucket_sample_count;
    g_air_quality_score_bucket_sum += score;
}

/// @brief Commits due five-minute averages into the 24-hour graph history.
/// @details The UI keeps live values separately. Histories are intentionally
/// averaged into compact buckets to avoid noisy spot checks and to keep memory
/// use bounded as the visible time range grows.
bool append_due_history_buckets(EnvironmentSensorStatus& status)
{
    if (absolute_time_diff_us(get_absolute_time(), g_next_local_condition_history_bucket_time) > 0)
    {
        return false;
    }

    const EnvironmentSensorStatus previous = status;
    if (g_bme_bucket_sample_count > 0U)
    {
        const int32_t average_temperature = static_cast<int32_t>(
            g_bme_temperature_bucket_sum / static_cast<int64_t>(g_bme_bucket_sample_count));
        const uint32_t average_pressure = static_cast<uint32_t>(
            g_bme_pressure_bucket_sum_pa / static_cast<uint64_t>(g_bme_bucket_sample_count));
        const uint32_t average_humidity = static_cast<uint32_t>(
            g_bme_humidity_bucket_sum_milli_percent /
            static_cast<uint64_t>(g_bme_bucket_sample_count));
        append_bme_history(status,
                           static_cast<int16_t>(
                               std::clamp<int32_t>(average_temperature, -32768, 32767)),
                           pressure_pa_to_deci_hpa(average_pressure),
                           humidity_milli_percent_to_centi_percent(average_humidity));
    }

    if (g_air_quality_bucket_sample_count > 0U)
    {
        const uint8_t average_score = static_cast<uint8_t>(
            g_air_quality_score_bucket_sum / g_air_quality_bucket_sample_count);
        append_air_quality_history(status, average_score);
    }

    reset_history_buckets();
    g_next_local_condition_history_bucket_time =
        make_timeout_time_ms(kLocalConditionHistoryBucketIntervalMs);
    return !visible_state_matches(previous, status);
}

/// @brief Samples only the BME280 once discovery has confirmed it is present.
/// @details This keeps graph data fresh without re-probing every board address
/// on each sample tick.
void sample_bme280(EnvironmentSensorStatus& status, uint32_t sample_time_ms)
{
    if (status.bme_variant != EnvironmentBmeVariant::Bme280)
    {
        return;
    }

    const sensors::BmeEnvironmentalReading reading = g_bme_sensor.read_environment();
    status.bme_reading_valid = reading.valid;
    status.bme_temperature_centi_celsius = reading.temperature_centi_celsius;
    status.bme_pressure_pa = reading.pressure_pa;
    status.bme_humidity_milli_percent = reading.humidity_milli_percent;
    status.bme_read_error = reading.last_error;

    if (reading.valid)
    {
        status.bme_last_read_ms = sample_time_ms;
        accumulate_bme_history_sample(reading);
    }
    else if (status.last_error == PICO_ERROR_NONE)
    {
        status.last_error = reading.last_error;
    }
}

/// @brief Samples the SGP40 raw VOC signal after discovery has found the sensor.
void sample_sgp40(EnvironmentSensorStatus& status, uint32_t sample_time_ms)
{
    const sensors::Sgp40AirQualityReading reading = g_sgp40_sensor.read_raw_signal(
        status.bme_temperature_centi_celsius, status.bme_humidity_milli_percent,
        status.bme_reading_valid);
    status.air_quality_raw_valid = reading.valid;
    status.air_quality_raw_signal = reading.raw_signal;
    status.air_quality_read_error = reading.last_error;

    if (reading.valid)
    {
        if (status.air_quality_baseline_raw_signal == 0U ||
            reading.raw_signal > status.air_quality_baseline_raw_signal)
        {
            status.air_quality_baseline_raw_signal = reading.raw_signal;
        }
        status.air_quality_score =
            air_quality_score_from_raw(reading.raw_signal, status.air_quality_baseline_raw_signal);
        status.air_quality_score_valid = true;
        status.air_quality_last_read_ms = sample_time_ms;
        accumulate_air_quality_history_sample(status.air_quality_score);
    }
    else
    {
        status.air_quality_score_valid = false;
        if (status.last_error == PICO_ERROR_NONE)
        {
            status.last_error = reading.last_error;
        }
    }
}

/// @brief Samples all local-condition sensors that are ready for periodic data.
bool sample_local_conditions(EnvironmentSensorStatus& status, uint32_t sample_time_ms)
{
    const EnvironmentSensorStatus previous = status;
    sample_bme280(status, sample_time_ms);
    sample_sgp40(status, sample_time_ms);
    (void)append_due_history_buckets(status);
    return !visible_state_matches(previous, status);
}

/// @brief Probes the expected Waveshare board addresses and updates status.
bool scan_expected_devices()
{
    EnvironmentSensorStatus next = g_status;
    next.detected_device_count = 0;
    next.last_error = PICO_ERROR_NONE;
    next.last_scan_ms = to_ms_since_boot(get_absolute_time());

    for (std::size_t index = 0; index < kWaveshareDevices.size(); ++index)
    {
        int result = PICO_ERROR_NONE;
        bool detected = false;
        if (kWaveshareDevices[index].device == EnvironmentSensorDevice::Bme280OrBme680)
        {
            const sensors::BmeEnvironmentalSensorStatus bme_status = g_bme_sensor.probe();
            next.bme_variant = bme_variant_from_status(bme_status);
            next.bme_chip_id = bme_status.chip_id;
            next.bme_last_error = bme_status.last_error;
            result = bme_status.last_error;
            detected = bme_status.state == sensors::BmeEnvironmentalSensorState::Present;
            next.bme_reading_valid = false;
            next.bme_read_error = PICO_ERROR_NONE;
        }
        else
        {
            detected =
                g_bus.probe_read(kWaveshareDevices[index].address, kProbeTimeoutUs, result);
        }
        next.devices[index].detected = detected;

        if (detected)
        {
            ++next.detected_device_count;
        }
        else if (next.last_error == PICO_ERROR_NONE)
        {
            next.last_error = result;
        }
    }

    (void)sample_local_conditions(next, next.last_scan_ms);
    for (EnvironmentSensorPresence& presence : next.devices)
    {
        if (presence.device == EnvironmentSensorDevice::Sgp40 && next.air_quality_raw_valid)
        {
            presence.detected = true;
        }
    }

    next.detected_device_count = 0U;
    for (const EnvironmentSensorPresence& presence : next.devices)
    {
        if (presence.detected)
        {
            ++next.detected_device_count;
        }
    }

    if (next.detected_device_count == kEnvironmentSensorDeviceCount)
    {
        next.health = EnvironmentSensorHealth::BoardDetected;
        ++next.successful_scan_count;
    }
    else if (next.detected_device_count > 0U)
    {
        next.health = EnvironmentSensorHealth::Partial;
        ++next.failed_scan_count;
    }
    else
    {
        next.health = EnvironmentSensorHealth::BoardMissing;
        ++next.failed_scan_count;
    }

    const bool changed = !visible_state_matches(g_status, next);
    g_status = next;
    g_next_scan_time = make_timeout_time_ms(kDiscoveryScanIntervalMs);
    g_next_local_condition_sample_time = make_timeout_time_ms(kLocalConditionSampleIntervalMs);
    if (absolute_time_diff_us(get_absolute_time(), g_next_local_condition_history_bucket_time) <= 0)
    {
        g_next_local_condition_history_bucket_time =
            make_timeout_time_ms(kLocalConditionHistoryBucketIntervalMs);
    }

    if (changed)
    {
        std::printf("Environment sensor discovery: %u/%u devices present, health=%u, err=%d\n",
                    static_cast<unsigned int>(g_status.detected_device_count),
                    static_cast<unsigned int>(kEnvironmentSensorDeviceCount),
                    static_cast<unsigned int>(g_status.health), g_status.last_error);
    }

    return changed;
}

} // namespace

void init()
{
    reset_status();

    if (!ENVIRONMENT_SENSOR_ENABLED)
    {
        return;
    }

    g_bus.configure(selected_i2c_instance(), ENVIRONMENT_SENSOR_I2C_SDA_GPIO,
                    ENVIRONMENT_SENSOR_I2C_SCL_GPIO, ENVIRONMENT_SENSOR_I2C_BAUDRATE_HZ);
    // The generic probes only read one byte, but BME280 calibration is read in
    // longer bursts. At 100 kHz I2C, the 26-byte trimming block needs more than
    // the one-byte probe timeout.
    g_bme_sensor.configure(&g_bus, kBmeI2cAddress, kSensorRegisterTimeoutUs);
    g_sgp40_sensor.configure(&g_bus, kSgp40I2cAddress, kSgp40RegisterTimeoutUs);

    const int init_result = g_bus.init();
    if (init_result != PICO_ERROR_NONE)
    {
        g_status.health = EnvironmentSensorHealth::Fault;
        g_status.last_error = init_result;
        std::printf("Environment sensor I2C init failed: bus=%d sda=%d scl=%d err=%d\n",
                    ENVIRONMENT_SENSOR_I2C_BUS, ENVIRONMENT_SENSOR_I2C_SDA_GPIO,
                    ENVIRONMENT_SENSOR_I2C_SCL_GPIO, init_result);
        return;
    }

    g_initialised = true;
    g_next_scan_time = make_timeout_time_ms(kInitialScanDelayMs);
    g_next_local_condition_sample_time = g_next_scan_time;
    g_next_local_condition_history_bucket_time =
        make_timeout_time_ms(kLocalConditionHistoryBucketIntervalMs);
    g_scan_requested = false;
    std::printf("Environment sensor I2C ready: bus=%d sda=%d scl=%d baud=%lu\n",
                ENVIRONMENT_SENSOR_I2C_BUS, ENVIRONMENT_SENSOR_I2C_SDA_GPIO,
                ENVIRONMENT_SENSOR_I2C_SCL_GPIO,
                static_cast<unsigned long>(ENVIRONMENT_SENSOR_I2C_BAUDRATE_HZ));
}

bool update()
{
    if (!ENVIRONMENT_SENSOR_ENABLED || !g_initialised)
    {
        return false;
    }

    const absolute_time_t now = get_absolute_time();
    const bool scan_due = absolute_time_diff_us(now, g_next_scan_time) <= 0;
    const bool local_condition_sample_due =
        absolute_time_diff_us(now, g_next_local_condition_sample_time) <= 0;
    if (!g_scan_requested && !scan_due && !local_condition_sample_due)
    {
        return false;
    }

    if (g_scan_requested || scan_due)
    {
        g_scan_requested = false;
        return scan_expected_devices();
    }

    const bool changed = sample_local_conditions(g_status, to_ms_since_boot(now));
    g_next_local_condition_sample_time = make_timeout_time_ms(kLocalConditionSampleIntervalMs);
    return changed;
}

void request_scan()
{
    g_scan_requested = true;
}

const EnvironmentSensorStatus& status()
{
    return g_status;
}

const char* air_quality_band_text(uint8_t score)
{
    if (score >= kAirQualityStableMinScore)
    {
        return "STABLE";
    }
    if (score >= kAirQualitySlightRiseMinScore)
    {
        return "SLIGHT RISE";
    }
    if (score >= kAirQualityVocRiseMinScore)
    {
        return "VOC RISE";
    }
    if (score >= kAirQualityHighVocMinScore)
    {
        return "HIGH VOC";
    }

    return "VERY HIGH";
}

const char* device_name(EnvironmentSensorDevice device)
{
    for (const ExpectedDevice& expected : kWaveshareDevices)
    {
        if (expected.device == device)
        {
            return expected.label;
        }
    }

    return "UNKNOWN";
}

} // namespace environment_sensor_manager
