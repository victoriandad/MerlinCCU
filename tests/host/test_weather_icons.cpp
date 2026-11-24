#include "weather_icons.h"

#include <array>
#include <cstring>

#include "framebuffer.h"
#include "golden_test_support.h"
#include "panel_config.h"
#include "test_framework.h"

using weather_icons::WeatherIcon;

HOST_TEST(weather_icons_match_both_sources_condition_words)
{
    // Home Assistant's conditions.
    EXPECT_TRUE(weather_icons::classify("sunny").icon == WeatherIcon::Sun);
    EXPECT_TRUE(weather_icons::classify("clear-night").icon == WeatherIcon::Moon);
    EXPECT_TRUE(weather_icons::classify("partlycloudy").icon == WeatherIcon::PartlyCloudy);
    EXPECT_TRUE(weather_icons::classify("lightning-rainy").icon == WeatherIcon::Thunder);
    EXPECT_TRUE(weather_icons::classify("snowy-rainy").icon == WeatherIcon::Sleet);
    EXPECT_TRUE(weather_icons::classify("windy-variant").icon == WeatherIcon::Wind);
    EXPECT_TRUE(weather_icons::classify("pouring").icon == WeatherIcon::HeavyRain);
    // Open-Meteo's words, and the friendly forms, in any case.
    EXPECT_TRUE(weather_icons::classify("Rain showers").icon == WeatherIcon::Rain);
    EXPECT_TRUE(weather_icons::classify("Snow showers").icon == WeatherIcon::Snow);
    EXPECT_TRUE(weather_icons::classify("Thunder").icon == WeatherIcon::Thunder);
    EXPECT_TRUE(weather_icons::classify("PARTLY CLOUDY").icon == WeatherIcon::PartlyCloudy);
    // Unknown words get no icon, so the provider's own text is shown.
    EXPECT_TRUE(weather_icons::classify("Weather").icon == WeatherIcon::None);
    EXPECT_TRUE(weather_icons::classify("").short_name == nullptr);
    EXPECT_TRUE(weather_icons::classify(nullptr).icon == WeatherIcon::None);
    EXPECT_TRUE(std::strcmp(weather_icons::classify("partlycloudy").short_name, "Part cloudy") ==
                0);
}

HOST_TEST(weather_icons_short_names_fit_the_conditions_column)
{
    for (const char* word :
         {"sunny", "clear-night", "partlycloudy", "cloudy", "fog", "rainy", "pouring", "snowy",
          "snowy-rainy", "lightning", "lightning-rainy", "hail", "windy", "windy-variant",
          "exceptional", "Clear", "Drizzle", "Rain", "Rain showers", "Snow", "Snow showers",
          "Thunder", "SLEET", "BREEZY", "LTNG RAIN"})
    {
        const weather_icons::Condition condition = weather_icons::classify(word);
        EXPECT_TRUE(condition.icon != WeatherIcon::None);
        EXPECT_TRUE(condition.short_name != nullptr &&
                    std::strlen(condition.short_name) <= weather_icons::kMaxShortNameLength);
    }
}

HOST_TEST(weather_icons_every_icon_is_drawn_and_none_is_blank)
{
    for (int i = 0; i < static_cast<int>(WeatherIcon::Count); ++i)
    {
        int lit = 0;
        for (int y = 0; y < weather_icons::kIconSize; ++y)
        {
            for (int x = 0; x < weather_icons::kIconSize; ++x)
            {
                lit += weather_icons::pixel(static_cast<WeatherIcon>(i), x, y) ? 1 : 0;
            }
        }
        EXPECT_TRUE(i == 0 ? lit == 0 : lit >= 10);
    }
    EXPECT_FALSE(weather_icons::pixel(WeatherIcon::Sun, weather_icons::kIconSize, 0));
}

HOST_TEST(weather_icons_sheet_matches_its_golden_image)
{
    // Every icon at panel size, in a row, and again at 4x so a person can
    // review the shapes in the golden image.
    std::array<uint8_t, static_cast<size_t>(kUiFbSize)> fb = {};
    constexpr int kScale = 4;
    for (int i = 1; i < static_cast<int>(WeatherIcon::Count); ++i)
    {
        const auto icon = static_cast<WeatherIcon>(i);
        weather_icons::draw(fb.data(), 4 + ((i - 1) * 12), 8, icon);
        const int cell = (i - 1);
        const int origin_x = 4 + ((cell % 6) * 40);
        const int origin_y = 40 + ((cell / 6) * 44);
        for (int y = 0; y < weather_icons::kIconSize; ++y)
        {
            for (int x = 0; x < weather_icons::kIconSize; ++x)
            {
                if (weather_icons::pixel(icon, x, y))
                {
                    framebuffer::fill_rect(fb.data(), origin_x + (x * kScale),
                                           origin_y + (y * kScale), kScale, kScale, true);
                }
            }
        }
    }
    EXPECT_TRUE(golden_test::check_golden("weather_icons_sheet", fb.data()));
}
