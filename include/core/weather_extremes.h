#pragma once

#include <cstdint>
#include <optional>

/// @brief When UK weather counts as exceptional (#202).
/// @details One table of thresholds shared by the forecast highlighting on
/// the Weather page and the WX TEMP and WX WIND alerts, so a highlighted cell
/// and its alert always agree. The figures follow how the Met Office
/// describes UK weather; docs/alerts.md gives the reasoning for each. Pure,
/// so host tests cover it.
namespace weather_extremes
{

/// @brief A set of hazards, one bit each.
using Hazards = uint8_t;

inline constexpr Hazards kNone = 0U;
inline constexpr Hazards kCold = 1U << 0U;
inline constexpr Hazards kHeat = 1U << 1U;
inline constexpr Hazards kWind = 1U << 2U;
inline constexpr Hazards kRain = 1U << 3U;
inline constexpr Hazards kSnow = 1U << 4U;
inline constexpr Hazards kFog = 1U << 5U;

/// @brief Hazards shown on each forecast column.
inline constexpr Hazards kTemperatureHazards = kCold | kHeat;
inline constexpr Hazards kWindHazards = kWind;
inline constexpr Hazards kConditionHazards = kRain | kSnow | kFog;

/// @brief Frost and ice: at or below freezing.
inline constexpr float kColdMaxCelsius = 0.0F;
/// @brief The top of the Met Office's county heatwave thresholds (25-28 C).
inline constexpr float kHeatMinCelsius = 28.0F;
/// @brief Beaufort force 8, a gale, starts at 39 mph mean wind.
inline constexpr float kGaleMinMph = 39.0F;
/// @brief Gusts where Met Office yellow wind warnings usually start inland.
inline constexpr float kGustMinMph = 50.0F;
/// @brief Met Office "heavy rain" is more than 4 mm in an hour.
inline constexpr float kHeavyRainHourMm = 4.0F;
/// @brief A day's rain typical of a yellow rain warning.
inline constexpr float kHeavyRainDayMm = 20.0F;
/// @brief Heavy snowfall in an hour.
inline constexpr float kHeavySnowHourCm = 1.0F;
/// @brief Lying snow where yellow snow warnings usually start.
inline constexpr float kHeavySnowDayCm = 5.0F;
/// @brief Met Office forecasts call fog under 200 m "dense".
inline constexpr float kDenseFogMaxMetres = 200.0F;
/// @brief Snow depth from melted precipitation when a provider gives only
/// the precipitation of a snowy period: the usual 10:1 ratio, 1 cm per mm.
inline constexpr float kSnowCmPerPrecipitationMm = 1.0F;

/// @brief The span a forecast row covers.
enum class Period : uint8_t
{
    Hour,
    Day,
};

/// @brief One forecast row's figures, each missing when the provider has
/// none. A day row gives its low and high; an hour row gives one temperature
/// as both.
struct Reading
{
    std::optional<float> low_celsius;
    std::optional<float> high_celsius;
    std::optional<float> wind_mph;
    std::optional<float> gust_mph;
    std::optional<float> rain_mm;
    std::optional<float> snow_cm;
    std::optional<float> visibility_m;
    /// The provider's condition says heavy rain (HA `pouring`, Open-Meteo
    /// codes 65 and 82), for when it gives no amount.
    bool heavy_rain_condition = false;
    /// The provider's condition says heavy snow (Open-Meteo codes 75 and 86).
    bool heavy_snow_condition = false;
};

/// @brief Returns the hazards a row's figures cross.
Hazards classify(const Reading& reading, Period period);

/// @brief True when a temperature is extreme cold.
constexpr bool is_cold(float celsius)
{
    return celsius <= kColdMaxCelsius;
}

/// @brief True when a temperature is exceptional heat.
constexpr bool is_hot(float celsius)
{
    return celsius >= kHeatMinCelsius;
}

/// @brief True when a mean wind or a gust is high.
constexpr bool is_high_wind(std::optional<float> wind_mph, std::optional<float> gust_mph)
{
    return (wind_mph.has_value() && *wind_mph >= kGaleMinMph) ||
           (gust_mph.has_value() && *gust_mph >= kGustMinMph);
}

} // namespace weather_extremes
