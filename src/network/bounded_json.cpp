#include "bounded_json.h"

#include <cstdlib>
#include <cstring>

namespace bounded_json
{

const char* skip_space(const char* cursor)
{
    while (cursor != nullptr &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n'))
    {
        ++cursor;
    }

    return cursor;
}

const char* find_bounded(const char* start, const char* end, const char* key)
{
    const size_t key_len = std::strlen(key);
    if (key_len == 0U || end <= start)
    {
        return nullptr;
    }

    const size_t range_len = static_cast<size_t>(end - start);
    if (key_len > range_len)
    {
        return nullptr;
    }

    for (const char* p = start; p <= end - key_len; ++p)
    {
        if (std::strncmp(p, key, key_len) == 0)
        {
            return p;
        }
    }

    return nullptr;
}

bool extract_bounded_string(const char* start, const char* end, const char* key, char* out,
                            size_t out_size)
{
    const char* cursor = find_bounded(start, end, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    cursor = skip_space(cursor + 1);
    if (cursor == nullptr || cursor >= end || *cursor != '"')
    {
        return false;
    }

    ++cursor;
    size_t write_index = 0U;
    while (cursor < end && *cursor != '"' && write_index + 1U < out_size)
    {
        if (*cursor == '\\' && (cursor + 1) < end)
        {
            ++cursor;
        }
        out[write_index++] = *cursor++;
    }

    out[write_index] = '\0';
    return write_index > 0U;
}

bool extract_bounded_number(const char* start, const char* end, const char* key,
                            double* out_value)
{
    const char* cursor = find_bounded(start, end, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    cursor = skip_space(cursor + 1);
    if (cursor == nullptr || cursor >= end || *cursor == '"')
    {
        return false;
    }

    char* parse_end = nullptr;
    const double value = std::strtod(cursor, &parse_end);
    if (parse_end == cursor)
    {
        return false;
    }

    *out_value = value;
    return true;
}

bool bounded_value_is_string(const char* start, const char* end, const char* key)
{
    const char* cursor = find_bounded(start, end, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    cursor = skip_space(cursor + 1);
    return cursor != nullptr && cursor < end && *cursor == '"';
}

const char* find_object_end(const char* obj_start, const char* buffer_end)
{
    if (obj_start >= buffer_end || *obj_start != '{')
    {
        return nullptr;
    }

    int depth = 0;
    bool in_string = false;
    for (const char* p = obj_start; p < buffer_end; ++p)
    {
        if (in_string)
        {
            if (*p == '\\' && (p + 1) < buffer_end)
            {
                ++p;
                continue;
            }
            if (*p == '"')
            {
                in_string = false;
            }
            continue;
        }

        if (*p == '"')
        {
            in_string = true;
        }
        else if (*p == '{')
        {
            ++depth;
        }
        else if (*p == '}')
        {
            --depth;
            if (depth == 0)
            {
                return p;
            }
        }
    }

    return nullptr;
}

} // namespace bounded_json
