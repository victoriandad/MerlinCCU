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

/// @brief Reads the numbers of a `history` array, from `cursor` (just past
/// its `[`), calling `take(index, value)` for each.
/// @return How many numbers it holds.
template <typename Take>
size_t scan_history(const char* cursor, const char* obj_end, Take&& take)
{
    size_t count = 0U;
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
        take(count++, static_cast<float>(value));
        cursor = end;
    }
    return count;
}

/// @brief Parses one share object's bounded `"history":[...]` array into the
/// fixed 24-point graph buffer, downsampling a longer array.
/// @details Prices are kept as given, fractions included, so a share priced
/// under 1 still has a shape (#178). Two passes over the text, counting then
/// picking, keep the parse free of a buffer for the whole array; the Pico
/// 2 W has no RAM to spare for one.
bool parse_bounded_history(const char* obj_start, const char* obj_end, ShareWatchEntry& share,
                           double fallback_price)
{
    const char* cursor = bounded_json::find_bounded(obj_start, obj_end, "\"history\":[");
    if (cursor == nullptr)
    {
        share.history_points.fill(static_cast<float>(fallback_price));
        return false;
    }
    cursor += std::strlen("\"history\":[");

    const size_t count = scan_history(cursor, obj_end, [](size_t, float) {});
    if (count == 0U)
    {
        share.history_points.fill(static_cast<float>(fallback_price));
        return false;
    }

    const size_t points = share.history_points.size();
    (void)scan_history(cursor, obj_end,
                       [&share, count, points](size_t index, float value)
                       {
                           // Every graph point whose source is this value.
                           for (size_t i = 0U; i < points; ++i)
                           {
                               const size_t source =
                                   count == 1U ? 0U : (i * (count - 1U)) / (points - 1U);
                               if (source == index)
                               {
                                   share.history_points[i] = value;
                               }
                           }
                       });
    return true;
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

                // The price the period's change is measured from (#178):
                // yesterday's close for Today, the first close otherwise.
                // Older feeds leave it out, and the graph uses its first point.
                double base = 0.0;
                share->base_valid =
                    bounded_json::extract_bounded_number(cursor, obj_end, "\"base\"", &base) &&
                    base > 0.0;
                share->base_price = share->base_valid ? static_cast<float>(base) : 0.0F;
                updated_any = true;
            }
        }

        cursor = obj_end + 1;
    }

    return updated_any;
}

} // namespace share_feed
