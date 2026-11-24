#include "weather_icons.h"

#include <array>
#include <cctype>
#include <cstring>

#include "framebuffer.h"

namespace weather_icons
{

namespace
{

/// @brief One icon as 9 rows of '#' (lit) and '.' (dark), kept as pictures
/// so the source shows what each looks like.
using Picture = std::array<const char*, kIconSize>;

/// @brief Packs a picture into one row mask per line, leftmost pixel in
/// bit 8.
using Bitmap = std::array<uint16_t, kIconSize>;

constexpr Bitmap pack(const Picture& picture)
{
    Bitmap bitmap = {};
    for (int y = 0; y < kIconSize; ++y)
    {
        uint16_t row = 0U;
        for (int x = 0; x < kIconSize; ++x)
        {
            row = static_cast<uint16_t>((row << 1U) | (picture[y][x] == '#' ? 1U : 0U));
        }
        bitmap[y] = row;
    }
    return bitmap;
}

// clang-format off
constexpr std::array<Bitmap, static_cast<size_t>(WeatherIcon::Count)> kBitmaps = {{
    pack({".........", ".........", ".........", ".........", ".........",
          ".........", ".........", ".........", "........."}), // None
    pack({"....#....", ".#.....#.", "...###...", "..#####..", "#.#####.#",
          "..#####..", "...###...", ".#.....#.", "....#...."}), // Sun
    pack({"...####..", "..##.....", ".##......", ".##......", ".##......",
          ".##......", ".##......", "..##.....", "...####.."}), // Moon
    pack({"..#......", "#.###....", ".#####...", "####.###.", ".##.#...#",
          "..#.....#", "..#######", ".........", "........."}), // PartlyCloudy
    pack({".........", "...###...", "..#...##.", ".#......#", "#.......#",
          "#.......#", ".#######.", ".........", "........."}), // Cloud
    pack({".........", "#######..", ".........", "..#######", ".........",
          "#######..", ".........", "..#######", "........."}), // Fog
    pack({"...###...", ".##...##.", "#.......#", ".#######.", ".........",
          ".#..#..#.", "#..#..#..", ".........", "........."}), // Rain
    pack({"...###...", ".##...##.", "#.......#", ".#######.", "#.#.#.#.#",
          ".#.#.#.#.", "#.#.#.#.#", ".#.#.#.#.", "........."}), // HeavyRain
    pack({"...###...", ".##...##.", "#.......#", ".#######.", ".........",
          ".#..#..#.", ".........", "...#..#..", "........."}), // Snow
    pack({"...###...", ".##...##.", "#.......#", ".#######.", ".........",
          ".#..#..#.", "#..#..#..", "..#..#..#", "........."}), // Sleet
    pack({"...###...", ".##...##.", "#.......#", ".###.###.", "....#....",
          "...##....", "....##...", ".....#...", "....#...."}), // Thunder
    pack({"...###...", ".##...##.", "#.......#", ".#######.", ".........",
          ".##..##..", ".##..##..", "....##..#", "....##..."}), // Hail
    pack({"......##.", ".......#.", "#######..", ".........", "#########",
          ".........", "#####.#..", "......#..", ".....##.."}), // Wind
    pack({"....#....", "...#.#...", "...#.#...", "..#.#.#..", "..#.#.#..",
          ".#.....#.", ".#..#..#.", "#.......#", "#########"}), // Exceptional
}};
// clang-format on

/// @brief A condition word (letters only, lower case) and what it shows.
struct Entry
{
    const char* key;
    WeatherIcon icon;
    const char* short_name;
};

// Home Assistant's weather conditions, then Open-Meteo's words
// (weather_normalisation.cpp). Short names fit kMaxShortNameLength.
constexpr std::array<Entry, 25> kEntries = {{
    {"sunny", WeatherIcon::Sun, "Sunny"},
    {"clearnight", WeatherIcon::Moon, "Clear"},
    {"partlycloudy", WeatherIcon::PartlyCloudy, "Part cloudy"},
    {"cloudy", WeatherIcon::Cloud, "Cloudy"},
    {"fog", WeatherIcon::Fog, "Fog"},
    {"rainy", WeatherIcon::Rain, "Rain"},
    {"pouring", WeatherIcon::HeavyRain, "Heavy rain"},
    {"snowy", WeatherIcon::Snow, "Snow"},
    {"snowyrainy", WeatherIcon::Sleet, "Sleet"},
    {"lightning", WeatherIcon::Thunder, "Thunder"},
    {"lightningrainy", WeatherIcon::Thunder, "Thundery"},
    {"hail", WeatherIcon::Hail, "Hail"},
    {"windy", WeatherIcon::Wind, "Windy"},
    {"windyvariant", WeatherIcon::Wind, "Breezy"},
    {"exceptional", WeatherIcon::Exceptional, "Exceptional"},
    {"clear", WeatherIcon::Sun, "Clear"},
    {"drizzle", WeatherIcon::Rain, "Drizzle"},
    {"rain", WeatherIcon::Rain, "Rain"},
    {"rainshowers", WeatherIcon::Rain, "Showers"},
    {"snow", WeatherIcon::Snow, "Snow"},
    {"snowshowers", WeatherIcon::Snow, "Snow showers"},
    {"thunder", WeatherIcon::Thunder, "Thunder"},
    {"sleet", WeatherIcon::Sleet, "Sleet"},
    {"breezy", WeatherIcon::Wind, "Breezy"},
    {"ltngrain", WeatherIcon::Thunder, "Thundery"},
}};

/// @brief Longest condition word kept for matching.
constexpr size_t kMaxKeyLength = 24U;

} // namespace

Condition classify(const char* condition)
{
    if (condition == nullptr)
    {
        return {WeatherIcon::None, nullptr};
    }
    char key[kMaxKeyLength + 1U] = {};
    size_t length = 0U;
    for (const char* c = condition; *c != '\0' && length < kMaxKeyLength; ++c)
    {
        const auto ch = static_cast<unsigned char>(*c);
        if (std::isalpha(ch) != 0)
        {
            key[length++] = static_cast<char>(std::tolower(ch));
        }
    }
    for (const Entry& entry : kEntries)
    {
        if (std::strcmp(key, entry.key) == 0)
        {
            return {entry.icon, entry.short_name};
        }
    }
    return {WeatherIcon::None, nullptr};
}

bool pixel(WeatherIcon icon, int x, int y)
{
    const auto index = static_cast<size_t>(icon);
    if (index >= kBitmaps.size() || x < 0 || y < 0 || x >= kIconSize || y >= kIconSize)
    {
        return false;
    }
    return (kBitmaps[index][static_cast<size_t>(y)] & (1U << (kIconSize - 1 - x))) != 0U;
}

void draw(uint8_t* fb, int x, int y, WeatherIcon icon)
{
    for (int row = 0; row < kIconSize; ++row)
    {
        for (int column = 0; column < kIconSize; ++column)
        {
            if (pixel(icon, column, row))
            {
                framebuffer::set_pixel(fb, x + column, y + row, true);
            }
        }
    }
}

} // namespace weather_icons
