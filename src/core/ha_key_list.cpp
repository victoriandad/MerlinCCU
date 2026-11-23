#include "ha_key_list.h"

#include <cstdio>

namespace ha_keys
{

namespace
{

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1U);
    }
    return text;
}

bool same_ignoring_case(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0U; i < a.size(); ++i)
    {
        if (lower(a[i]) != lower(b[i]))
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<uint64_t> parse_list(std::string_view text, NameFn name_of,
                                   std::string_view* bad_item)
{
    uint64_t mask = 0U;
    while (!text.empty())
    {
        const size_t comma = text.find(',');
        const std::string_view item = trim(text.substr(0U, comma));
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1U);
        if (item.empty())
        {
            continue;
        }

        bool found = false;
        for (size_t i = 0U; i < static_cast<size_t>(ButtonId::Count); ++i)
        {
            const auto id = static_cast<ButtonId>(i);
            const char* legend = name_of(id);
            if (legend != nullptr && same_ignoring_case(item, legend))
            {
                mask |= bit(id);
                found = true;
                break;
            }
        }
        if (!found)
        {
            if (bad_item != nullptr)
            {
                *bad_item = item;
            }
            return std::nullopt;
        }
    }
    return mask;
}

void format_list(uint64_t mask, NameFn name_of, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    out[0] = '\0';
    size_t used = 0U;
    for (size_t i = 0U; i < static_cast<size_t>(ButtonId::Count); ++i)
    {
        const auto id = static_cast<ButtonId>(i);
        const char* legend = name_of(id);
        if (!contains(mask, id) || legend == nullptr)
        {
            continue;
        }
        const int written =
            std::snprintf(out + used, out_size - used, "%s%s", used == 0U ? "" : ", ", legend);
        if (written < 0 || static_cast<size_t>(written) >= out_size - used)
        {
            return;
        }
        used += static_cast<size_t>(written);
    }
}

void slug(const char* legend, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    size_t used = 0U;
    out[0] = '\0';
    for (size_t i = 0U; legend != nullptr && legend[i] != '\0'; ++i)
    {
        const char c = lower(legend[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        // A space becomes "_"; other symbols their hex code, so "/" and "."
        // stay distinct ("_2f", "_2e").
        char piece[4] = {};
        if (plain)
        {
            piece[0] = c;
        }
        else if (c == ' ')
        {
            piece[0] = '_';
        }
        else
        {
            std::snprintf(piece, sizeof(piece), "_%02x", static_cast<unsigned char>(c));
        }
        const int written = std::snprintf(out + used, out_size - used, "%s", piece);
        if (written < 0 || static_cast<size_t>(written) >= out_size - used)
        {
            out[used] = '\0';
            return;
        }
        used += static_cast<size_t>(written);
    }
}

} // namespace ha_keys
