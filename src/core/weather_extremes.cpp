#include "weather_extremes.h"

namespace weather_extremes
{

Hazards classify(const Reading& reading, Period period)
{
    const bool kDay = period == Period::Day;
    Hazards hazards = kNone;
    if (reading.low_celsius.has_value() && is_cold(*reading.low_celsius))
    {
        hazards |= kCold;
    }
    if (reading.high_celsius.has_value() && is_hot(*reading.high_celsius))
    {
        hazards |= kHeat;
    }
    if (is_high_wind(reading.wind_mph, reading.gust_mph))
    {
        hazards |= kWind;
    }
    const float kRainLimit = kDay ? kHeavyRainDayMm : kHeavyRainHourMm;
    if (reading.heavy_rain_condition ||
        (reading.rain_mm.has_value() && *reading.rain_mm >= kRainLimit))
    {
        hazards |= kRain;
    }
    const float kSnowLimit = kDay ? kHeavySnowDayCm : kHeavySnowHourCm;
    if (reading.heavy_snow_condition ||
        (reading.snow_cm.has_value() && *reading.snow_cm >= kSnowLimit))
    {
        hazards |= kSnow;
    }
    if (reading.visibility_m.has_value() && *reading.visibility_m < kDenseFogMaxMetres)
    {
        hazards |= kFog;
    }
    return hazards;
}

} // namespace weather_extremes
