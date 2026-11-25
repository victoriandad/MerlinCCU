#pragma once

#include <cstddef>
#include <cstdint>

/// @brief Small weather icons for the forecast tables (#10).
/// @details One 9x9 bitmap per kind of weather, drawn beside the conditions
/// text. Both sources' condition words map onto them: Home Assistant's
/// (`partlycloudy`, `lightning-rainy`, ...) and Open-Meteo's (`Rain
/// showers`, ...). The set is kept small so each icon stays legible at one
/// pixel per dot on the monochrome panel; the whole set is a few hundred
/// bytes of flash. Pure, so host tests cover it.
namespace weather_icons
{

/// @brief Kinds of weather with an icon.
enum class WeatherIcon : uint8_t
{
    None, ///< Unknown condition: no icon, text only.
    Sun,
    Moon,
    PartlyCloudy,
    Cloud,
    Fog,
    Rain,
    HeavyRain,
    Snow,
    Sleet,
    Thunder,
    Hail,
    Wind,
    Exceptional,
    Count,
};

/// @brief Width and height of every icon, in pixels.
inline constexpr int kIconSize = 9;

/// @brief Longest short condition name, in characters; it fits the forecast
/// table's conditions column beside the icon.
inline constexpr size_t kMaxShortNameLength = 12U;

/// @brief The icon and short name for a condition word from either source,
/// matched ignoring case, spaces, hyphens and underscores.
struct Condition
{
    WeatherIcon icon;
    /// Mixed-case display text, e.g. "Part cloudy"; nullptr when the
    /// condition is not recognised (show the provider's own words).
    const char* short_name;
};

/// @brief Looks up a provider's condition word.
Condition classify(const char* condition);

/// @brief True when pixel (`x`, `y`) of `icon` is lit; false outside the
/// icon or for WeatherIcon::None.
bool pixel(WeatherIcon icon, int x, int y);

/// @brief Draws `icon` with its top-left corner at (`x`, `y`), its lit
/// pixels set to `on` (false draws it dark on a highlight).
void draw(uint8_t* fb, int x, int y, WeatherIcon icon, bool on = true);

} // namespace weather_icons
