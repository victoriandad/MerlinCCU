#include "environment_sensor_manager.h"

// environment_sensor_manager.cpp itself (the firmware implementation) drives
// real I2C hardware (hardware/i2c.h, pico/stdlib.h) and isn't buildable here
// -- this host project has no Pico SDK toolchain. screens.cpp's Local
// Conditions rendering calls air_quality_band_text() (a pure score->label
// mapping, not a hardware read) to format the air-quality metric, so
// something must satisfy that symbol at link time for host_tests. This is a
// faithful copy of the real thresholds/labels, not a fake -- kept in sync by
// hand since it's a small, rarely-changed cosmetic banding table.
namespace environment_sensor_manager
{

namespace
{
constexpr uint8_t kAirQualityStableMinScore = 95U;
constexpr uint8_t kAirQualitySlightRiseMinScore = 85U;
constexpr uint8_t kAirQualityVocRiseMinScore = 70U;
constexpr uint8_t kAirQualityHighVocMinScore = 50U;
} // namespace

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

} // namespace environment_sensor_manager
