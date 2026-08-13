#include "share_feed_parser.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bounded_json.h"
#include "text_utils.h"

namespace share_feed
{

namespace
{
using text_utils::copy_text;

constexpr size_t kMaxParsedHistoryValues = 256U;

/// @brief Formats a price compactly enough for one bracketed softkey line.
void format_price_text(double price, std::array<char, 12>& out)
{
    out.fill('\0');
    if (price >= 1000.0 && price < 10000.0)
    {
        const long tenths = std::lround(price * 10.0);
        const int whole = static_cast<int>(tenths / 10L);
        const int thousands = whole / 1000;
        const int remainder = whole % 1000;
        const int decimal = static_cast<int>(tenths % 10L);
        std::snprintf(out.data(), out.size(), "%d,%03d.%d", thousands, remainder, decimal);
        return;
    }

    std::snprintf(out.data(), out.size(), "%.1f", price);
}

/// @brief Converts a parsed price into the uint16 graph range used by the renderer.
uint16_t graph_value_from_price(double price)
{
    const long rounded = std::lround(price);
    if (rounded < 0L)
    {
        return 0U;
    }
    if (rounded > 65535L)
    {
        return 65535U;
    }

    return static_cast<uint16_t>(rounded);
}

/// @brief Downsamples arbitrary close values into the fixed 24-point CCU graph buffer.
void copy_history_points(const std::array<uint16_t, kMaxParsedHistoryValues>& parsed_values,
                         size_t count, ShareWatchEntry& share, uint16_t fallback_price)
{
    if (count == 0U)
    {
        share.history_points.fill(fallback_price);
        return;
    }

    for (size_t i = 0U; i < share.history_points.size(); ++i)
    {
        const size_t source_index =
            (count == 1U) ? 0U : ((i * (count - 1U)) / (share.history_points.size() - 1U));
        share.history_points[i] = parsed_values[source_index];
    }
}

/// @brief Parses one share object's bounded `"history":[...]` array into graph points.
bool parse_bounded_history(const char* obj_start, const char* obj_end, ShareWatchEntry& share,
                           double fallback_price)
{
    const char* cursor = bounded_json::find_bounded(obj_start, obj_end, "\"history\":[");
    if (cursor == nullptr)
    {
        copy_history_points({}, 0U, share, graph_value_from_price(fallback_price));
        return false;
    }

    cursor += std::strlen("\"history\":[");
    size_t value_count = 0U;
    std::array<uint16_t, kMaxParsedHistoryValues> parsed_values = {};

    while (cursor < obj_end)
    {
        cursor = bounded_json::skip_space(cursor);
        if (cursor >= obj_end || *cursor == ']')
        {
            break;
        }
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }

        char* end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor)
        {
            break;
        }
        if (value_count < parsed_values.size())
        {
            parsed_values[value_count++] = graph_value_from_price(value);
        }
        cursor = end;
    }

    copy_history_points(parsed_values, value_count, share, graph_value_from_price(fallback_price));
    return value_count > 0U;
}

/// @brief Finds the watched share entry matching `symbol`, or nullptr.
ShareWatchEntry* find_watched_share(std::array<ShareWatchEntry, kMaxWatchedShares>& watched_shares,
                                    uint8_t share_count, const char* symbol)
{
    if (symbol == nullptr || symbol[0] == '\0')
    {
        return nullptr;
    }

    for (uint8_t i = 0U; i < share_count; ++i)
    {
        if (std::strcmp(watched_shares[i].symbol.data(), symbol) == 0)
        {
            return &watched_shares[i];
        }
    }

    return nullptr;
}

} // namespace

bool parse_shares_feed_response(const char* body, const char* buffer_end,
                                std::array<ShareWatchEntry, kMaxWatchedShares>& watched_shares,
                                uint8_t share_count)
{
    if (body == nullptr || buffer_end == nullptr)
    {
        return false;
    }

    const char* array_marker = std::strstr(body, "\"shares\":[");
    if (array_marker == nullptr)
    {
        return false;
    }

    const char* cursor = array_marker + std::strlen("\"shares\":[");
    bool updated_any = false;

    while (cursor < buffer_end)
    {
        cursor = bounded_json::skip_space(cursor);
        if (cursor >= buffer_end || *cursor == ']')
        {
            break;
        }
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }
        if (*cursor != '{')
        {
            break;
        }

        const char* obj_end = bounded_json::find_object_end(cursor, buffer_end);
        if (obj_end == nullptr)
        {
            // Truncated final object (response didn't fit) -- stop here and
            // keep whatever shares were already parsed.
            break;
        }

        char symbol[10] = {};
        if (bounded_json::extract_bounded_string(cursor, obj_end, "\"symbol\"", symbol,
                                                 sizeof(symbol)))
        {
            ShareWatchEntry* share = find_watched_share(watched_shares, share_count, symbol);
            char data_state[16] = {};
            const bool has_state = bounded_json::extract_bounded_string(
                cursor, obj_end, "\"data_state\"", data_state, sizeof(data_state));
            if (share != nullptr && (!has_state || std::strcmp(data_state, "live") == 0))
            {
                char name[24] = {};
                if (bounded_json::extract_bounded_string(cursor, obj_end, "\"name\"", name,
                                                         sizeof(name)))
                {
                    copy_text(share->display_name, name);
                }

                char exchange[8] = {};
                if (bounded_json::extract_bounded_string(cursor, obj_end, "\"exchange\"", exchange,
                                                         sizeof(exchange)))
                {
                    copy_text(share->exchange, exchange);
                }

                char currency[8] = {};
                if (bounded_json::extract_bounded_string(cursor, obj_end, "\"currency\"", currency,
                                                         sizeof(currency)))
                {
                    copy_text(share->currency, currency);
                }

                double price = 0.0;
                bool have_price =
                    bounded_json::extract_bounded_number(cursor, obj_end, "\"price\"", &price);
                if (!have_price)
                {
                    char price_text[16] = {};
                    if (bounded_json::extract_bounded_string(cursor, obj_end, "\"price\"",
                                                             price_text, sizeof(price_text)))
                    {
                        char* end = nullptr;
                        price = std::strtod(price_text, &end);
                        have_price = end != price_text;
                    }
                }
                if (have_price)
                {
                    format_price_text(price, share->price_text);
                }

                char change_text[12] = {};
                if (bounded_json::extract_bounded_string(cursor, obj_end, "\"change\"",
                                                         change_text, sizeof(change_text)))
                {
                    copy_text(share->change_text, change_text);
                }

                parse_bounded_history(cursor, obj_end, *share, price);
                updated_any = true;
            }
        }

        cursor = obj_end + 1;
    }

    return updated_any;
}

} // namespace share_feed
