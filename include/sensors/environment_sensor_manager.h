#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief Owns discovery state for the optional Waveshare Pico environment board.
/// @details Full sensor drivers will stay behind this manager. The UI should
/// consume compact snapshots from here rather than storing raw histories or
/// driver-private state in `ConsoleState`.
namespace environment_sensor_manager
{

/// @brief Known sensor positions on the Waveshare Pico Environment Sensor board.
enum class EnvironmentSensorDevice : uint8_t
{
    Tsl2591 = 0,
    Bme280OrBme680,
    Icm20948,
    Ltr390,
    Sgp40,
    Count,
};

inline constexpr std::size_t kEnvironmentSensorDeviceCount =
    static_cast<std::size_t>(EnvironmentSensorDevice::Count);
inline constexpr std::size_t kLocalConditionHistoryPointCount = 288U;

/// @brief Returns the operator-facing VOC-change label for a local 0-100 score.
const char* air_quality_band_text(uint8_t score);

/// @brief High-level board identity detected by this manager.
enum class EnvironmentSensorBoard : uint8_t
{
    None = 0,
    WavesharePicoEnvironmentSensor,
};

/// @brief Coarse health state for diagnostics and future CCU alerts.
enum class EnvironmentSensorHealth : uint8_t
{
    Disabled = 0,
    BusReady,
    BoardMissing,
    Partial,
    BoardDetected,
    Fault,
};

/// @brief BME-family device variant detected at the Waveshare pressure sensor address.
enum class EnvironmentBmeVariant : uint8_t
{
    NotChecked = 0,
    Missing,
    Bme280,
    Bme680,
    Unknown,
    Fault,
};

/// @brief Discovery result for one expected I2C device.
struct EnvironmentSensorPresence
{
    EnvironmentSensorDevice device;
    uint8_t i2c_address;
    bool detected;
};

/// @brief Compact status snapshot exposed by the environment subsystem.
struct EnvironmentSensorStatus
{
    bool enabled;
    EnvironmentSensorBoard board;
    EnvironmentSensorHealth health;
    std::array<EnvironmentSensorPresence, kEnvironmentSensorDeviceCount> devices;
    uint8_t detected_device_count;
    uint32_t last_scan_ms;
    uint32_t successful_scan_count;
    uint32_t failed_scan_count;
    int last_error;
    int8_t i2c_bus;
    int8_t sda_gpio;
    int8_t scl_gpio;
    uint32_t baudrate_hz;
    EnvironmentBmeVariant bme_variant;
    uint8_t bme_chip_id;
    int bme_last_error;
    /// @brief True when BME280 fixed-point readings below contain a successful sample.
    bool bme_reading_valid;
    /// @brief Temperature in hundredths of a degree Celsius.
    int32_t bme_temperature_centi_celsius;
    /// @brief Pressure in pascals.
    uint32_t bme_pressure_pa;
    /// @brief Relative humidity in thousandths of a percent.
    uint32_t bme_humidity_milli_percent;
    /// @brief System uptime timestamp for the last successful BME280 sample.
    uint32_t bme_last_read_ms;
    /// @brief Pico SDK style status code for the latest BME280 read attempt.
    int bme_read_error;
    /// @brief Number of valid five-minute average BME entries in each rolling history array.
    uint16_t bme_history_count;
    /// @brief Rolling BME280 temperature history in hundredths of a degree Celsius.
    std::array<int16_t, kLocalConditionHistoryPointCount> bme_temperature_history_centi_celsius;
    /// @brief Rolling BME280 pressure history in tenths of a hectopascal.
    std::array<uint16_t, kLocalConditionHistoryPointCount> bme_pressure_history_deci_hpa;
    /// @brief Rolling BME280 relative-humidity history in hundredths of a percent.
    std::array<uint16_t, kLocalConditionHistoryPointCount> bme_humidity_history_centi_percent;
    /// @brief True when the latest SGP40 raw VOC reading is valid.
    bool air_quality_raw_valid;
    /// @brief Latest compensated SGP40 SRAW_VOC value.
    uint16_t air_quality_raw_signal;
    /// @brief Highest raw SGP40 signal seen locally, used as the clean-air baseline.
    uint16_t air_quality_baseline_raw_signal;
    /// @brief True when the local air-quality score below is valid.
    bool air_quality_score_valid;
    /// @brief Local air-quality score where 100 is best and 0 is worst.
    uint8_t air_quality_score;
    /// @brief System uptime timestamp for the last successful SGP40 sample.
    uint32_t air_quality_last_read_ms;
    /// @brief Pico SDK style status code for the latest SGP40 read attempt.
    int air_quality_read_error;
    /// @brief Number of valid five-minute average VOC-score entries in the rolling history.
    uint16_t air_quality_history_count;
    /// @brief Rolling local VOC-change score history.
    std::array<uint8_t, kLocalConditionHistoryPointCount> air_quality_history_score;
};

/// @brief Initialises the optional I2C environment sensor bus.
void init();

/// @brief Advances deferred discovery work.
/// @return `true` when the externally visible discovery state changed.
bool update();

/// @brief Requests a scan on the next call to `update()`.
void request_scan();

/// @brief Returns the latest compact discovery snapshot.
const EnvironmentSensorStatus& status();

/// @brief Returns a stable diagnostic label for an expected sensor device.
const char* device_name(EnvironmentSensorDevice device);

} // namespace environment_sensor_manager
