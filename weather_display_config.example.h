#pragma once

/// @brief Optional Home Assistant weather display settings.
/// @details Copy this file to `weather_display_config.h` only if you want the
/// Home page to show current weather from a Home Assistant `weather.*` entity.
///
/// This does not talk to an internet weather API directly. Instead, MerlinCCU
/// reuses the existing Home Assistant REST connection and reads one configured
/// `weather` entity via `GET /api/states/<entity_id>`, plus hourly forecast
/// data via Home Assistant's `weather/get_forecasts` service when available.
///
/// Quick start:
/// 1. Copy this file to `weather_display_config.h`.
/// 2. Set `HOME_ASSISTANT_WEATHER_ENTITY_ID` to a real Home Assistant weather
///    entity, for example `weather.forecast_home`.
/// 3. Optionally set `HOME_ASSISTANT_WEATHER_SOURCE_LABEL` as a fallback label
///    used only when the weather entity does not expose a provider hint.
/// 4. Optionally set `HOME_ASSISTANT_SUN_ENTITY_ID` if you want sunrise and
///    sunset times sourced from something other than Home Assistant's default
///    `sun.sun` entity.
/// 5. Rebuild and flash the firmware.
///
/// If this file is absent, weather display stays disabled and the Home page
/// will show `WEATHER OFF`.

inline constexpr char HOME_ASSISTANT_WEATHER_ENTITY_ID[] = "weather.forecast_home";
inline constexpr char HOME_ASSISTANT_WEATHER_SOURCE_LABEL[] = "Weather";
inline constexpr char HOME_ASSISTANT_SUN_ENTITY_ID[] = "sun.sun";
