#include "environment_sensor_manager.h"

#include <cstdio>

#include "bme_environmental_sensor.h"
#include "hardware/i2c.h"
#include "i2c_bus.h"
#include "pico/error.h"
#include "pico/stdlib.h"

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

constexpr std::array<ExpectedDevice, kEnvironmentSensorDeviceCount> kWaveshareDevices = {{
    {EnvironmentSensorDevice::Tsl2591, 0x29U, "TSL2591"},
    {EnvironmentSensorDevice::Bme280OrBme680, kBmeI2cAddress, "BME280/BME680"},
    {EnvironmentSensorDevice::Icm20948, 0x68U, "ICM20948"},
    {EnvironmentSensorDevice::Ltr390, 0x53U, "LTR390"},
    {EnvironmentSensorDevice::Sgp40, 0x59U, "SGP40"},
}};

inline constexpr uint32_t kInitialScanDelayMs = 10000U;
inline constexpr uint32_t kDiscoveryScanIntervalMs = 30000U;
inline constexpr uint32_t kProbeTimeoutUs = 1000U;

sensors::I2cBus g_bus;
sensors::BmeEnvironmentalSensor g_bme_sensor;
EnvironmentSensorStatus g_status{};
absolute_time_t g_next_scan_time{};
bool g_scan_requested = false;
bool g_initialised = false;

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
        lhs.bme_last_error != rhs.bme_last_error)
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
    g_bme_sensor.configure(&g_bus, kBmeI2cAddress, kProbeTimeoutUs);

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
    if (!g_scan_requested && !scan_due)
    {
        return false;
    }

    g_scan_requested = false;
    return scan_expected_devices();
}

void request_scan()
{
    g_scan_requested = true;
}

const EnvironmentSensorStatus& status()
{
    return g_status;
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
