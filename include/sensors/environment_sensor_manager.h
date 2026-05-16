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
