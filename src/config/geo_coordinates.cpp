#include "geo_coordinates.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace geo_coordinates
{

namespace
{

constexpr double kMaxLatitude = 90.0;
constexpr double kMaxLongitude = 180.0;
constexpr double kMinutesPerDegree = 60.0;
constexpr double kSecondsPerDegree = 3600.0;

/// @brief Most tokens a position can need: two values of three parts and
/// two hemisphere letters, plus a separating comma.
constexpr size_t kMaxTokens = 9U;

/// @brief Longest number accepted, in characters.
constexpr size_t kMaxNumberLength = 15U;

enum class TokenKind : uint8_t
{
    Number,
    Hemisphere,
    Comma,
};

struct Token
{
    TokenKind kind;
    double value;   ///< Number: its absolute value.
    bool negative;  ///< Number: written with a leading minus.
    bool has_sign;  ///< Number: written with a leading + or -.
    bool fraction;  ///< Number: has a decimal point.
    char hemisphere; ///< Hemisphere: upper-case N, S, E or W.
};

using Tokens = std::array<Token, kMaxTokens>;

/// @brief Length of a degree, minute or second mark at `text[i]` (ASCII or
/// UTF-8), or 0 when there is none.
size_t mark_length(std::string_view text, size_t i)
{
    const auto byte = [&text](size_t at) { return static_cast<uint8_t>(text[at]); };
    if (text[i] == '\'' || text[i] == '"' || text[i] == ':')
    {
        return 1U;
    }
    // ° U+00B0 and º U+00BA (often typed for degrees).
    if (byte(i) == 0xC2U && i + 1U < text.size() && (byte(i + 1U) == 0xB0U || byte(i + 1U) == 0xBAU))
    {
        return 2U;
    }
    // ′ U+2032 and ″ U+2033.
    if (byte(i) == 0xE2U && i + 2U < text.size() && byte(i + 1U) == 0x80U &&
        (byte(i + 2U) == 0xB2U || byte(i + 2U) == 0xB3U))
    {
        return 3U;
    }
    return 0U;
}

/// @brief Splits `text` into numbers, hemisphere letters and commas; marks
/// and spaces only separate. False on anything else, or too many tokens.
bool tokenize(std::string_view text, Tokens& tokens, size_t& count)
{
    count = 0U;
    size_t i = 0U;
    while (i < text.size())
    {
        const char c = text[i];
        const size_t mark = mark_length(text, i);
        if (c == ' ' || c == '\t' || mark > 0U)
        {
            i += (mark > 0U) ? mark : 1U;
            continue;
        }
        if (count == tokens.size())
        {
            return false;
        }
        Token& token = tokens[count++];
        token = {};
        if (c == ',')
        {
            token.kind = TokenKind::Comma;
            ++i;
            continue;
        }
        const char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        if (upper == 'N' || upper == 'S' || upper == 'E' || upper == 'W')
        {
            token.kind = TokenKind::Hemisphere;
            token.hemisphere = upper;
            ++i;
            continue;
        }

        token.kind = TokenKind::Number;
        size_t start = i;
        if (c == '+' || c == '-')
        {
            token.has_sign = true;
            token.negative = c == '-';
            ++i;
            start = i;
        }
        bool digits = false;
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'))
        {
            if (text[i] == '.')
            {
                if (token.fraction)
                {
                    return false;
                }
                token.fraction = true;
            }
            else
            {
                digits = true;
            }
            ++i;
        }
        const size_t length = i - start;
        if (!digits || length > kMaxNumberLength)
        {
            return false;
        }
        char number[kMaxNumberLength + 1U] = {};
        for (size_t k = 0U; k < length; ++k)
        {
            number[k] = text[start + k];
        }
        token.value = std::strtod(number, nullptr);
    }
    return true;
}

/// @brief Combines 1-3 numbers into degrees. Only the last part may have a
/// fraction, and minutes and seconds must be under 60.
std::optional<double> combine(const Token* parts, size_t count)
{
    if (count == 0U || count > 3U)
    {
        return std::nullopt;
    }
    constexpr std::array<double, 3> kScale = {1.0, kMinutesPerDegree, kSecondsPerDegree};
    double degrees = 0.0;
    for (size_t i = 0U; i < count; ++i)
    {
        const Token& part = parts[i];
        if (part.kind != TokenKind::Number || (i > 0U && part.has_sign) ||
            (part.fraction && i + 1U < count) || (i > 0U && part.value >= kMinutesPerDegree))
        {
            return std::nullopt;
        }
        degrees += part.value / kScale[i];
    }
    return parts[0].negative ? -degrees : degrees;
}

bool in_range(const GeoPoint& point)
{
    return std::fabs(point.latitude) <= kMaxLatitude && std::fabs(point.longitude) <= kMaxLongitude;
}

/// @brief Reads two values marked with hemisphere letters, all before or all
/// after their numbers.
std::optional<GeoPoint> parse_with_hemispheres(const Tokens& tokens, size_t count)
{
    // Commas are optional between lettered values.
    std::array<Token, kMaxTokens> kept = {};
    size_t kept_count = 0U;
    for (size_t i = 0U; i < count; ++i)
    {
        if (tokens[i].kind != TokenKind::Comma)
        {
            kept[kept_count++] = tokens[i];
        }
    }

    const bool prefix = kept[0].kind == TokenKind::Hemisphere;
    std::optional<double> latitude;
    std::optional<double> longitude;
    size_t i = 0U;
    while (i < kept_count)
    {
        size_t numbers_start = prefix ? i + 1U : i;
        size_t end = numbers_start;
        while (end < kept_count && kept[end].kind == TokenKind::Number)
        {
            ++end;
        }
        const size_t hemisphere_at = prefix ? i : end;
        if (hemisphere_at >= kept_count || kept[hemisphere_at].kind != TokenKind::Hemisphere)
        {
            return std::nullopt;
        }
        for (size_t k = numbers_start; k < end; ++k)
        {
            if (kept[k].has_sign)
            {
                return std::nullopt; // A letter and a sign would contradict.
            }
        }
        const std::optional<double> degrees = combine(&kept[numbers_start], end - numbers_start);
        if (!degrees)
        {
            return std::nullopt;
        }
        const char hemisphere = kept[hemisphere_at].hemisphere;
        const bool is_latitude = hemisphere == 'N' || hemisphere == 'S';
        std::optional<double>& slot = is_latitude ? latitude : longitude;
        if (slot)
        {
            return std::nullopt; // Two latitudes, or two longitudes.
        }
        slot = (hemisphere == 'S' || hemisphere == 'W') ? -*degrees : *degrees;
        i = prefix ? end : end + 1U;
    }
    if (!latitude || !longitude)
    {
        return std::nullopt;
    }
    return GeoPoint{*latitude, *longitude};
}

/// @brief Reads two unlettered values, latitude first, split by a comma or
/// into equal halves.
std::optional<GeoPoint> parse_signed(const Tokens& tokens, size_t count)
{
    size_t comma = count;
    for (size_t i = 0U; i < count; ++i)
    {
        if (tokens[i].kind == TokenKind::Comma)
        {
            if (comma != count)
            {
                return std::nullopt;
            }
            comma = i;
        }
    }
    size_t first_count = 0U;
    size_t second_start = 0U;
    if (comma != count)
    {
        first_count = comma;
        second_start = comma + 1U;
    }
    else
    {
        if (count % 2U != 0U)
        {
            return std::nullopt;
        }
        first_count = count / 2U;
        second_start = first_count;
    }
    const std::optional<double> latitude = combine(&tokens[0], first_count);
    const std::optional<double> longitude = combine(&tokens[second_start], count - second_start);
    if (!latitude || !longitude)
    {
        return std::nullopt;
    }
    return GeoPoint{*latitude, *longitude};
}

} // namespace

std::optional<GeoPoint> parse(std::string_view text)
{
    Tokens tokens = {};
    size_t count = 0U;
    if (!tokenize(text, tokens, count) || count == 0U)
    {
        return std::nullopt;
    }
    bool lettered = false;
    for (size_t i = 0U; i < count; ++i)
    {
        lettered = lettered || tokens[i].kind == TokenKind::Hemisphere;
    }
    const std::optional<GeoPoint> point =
        lettered ? parse_with_hemispheres(tokens, count) : parse_signed(tokens, count);
    if (!point || !in_range(*point))
    {
        return std::nullopt;
    }
    return point;
}

bool format_decimal(const GeoPoint& point, char* out, size_t out_size)
{
    const int written = std::snprintf(out, out_size, "%.6f,%.6f", point.latitude, point.longitude);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool format_degrees_minutes(const GeoPoint& point, const char* degree_mark, char* out,
                            size_t out_size)
{
    // Rounded to hundredths of a minute first, so 59.996' carries into the
    // next degree instead of printing as 60.00'.
    const auto split = [](double value, long& degrees, double& minutes)
    {
        const long hundredths = std::lround(std::fabs(value) * kMinutesPerDegree * 100.0);
        degrees = hundredths / 6000L;
        minutes = static_cast<double>(hundredths % 6000L) / 100.0;
    };
    long lat_degrees = 0L;
    long lon_degrees = 0L;
    double lat_minutes = 0.0;
    double lon_minutes = 0.0;
    split(point.latitude, lat_degrees, lat_minutes);
    split(point.longitude, lon_degrees, lon_minutes);
    const int written = std::snprintf(
        out, out_size, "%c%02ld%s%05.2f' %c%03ld%s%05.2f'", point.latitude < 0.0 ? 'S' : 'N',
        lat_degrees, degree_mark, lat_minutes, point.longitude < 0.0 ? 'W' : 'E', lon_degrees,
        degree_mark, lon_minutes);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

} // namespace geo_coordinates
