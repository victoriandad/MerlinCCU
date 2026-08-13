#include "weather_json_scan.h"

#include <cstring>

namespace weather_json
{

bool extract_json_string_value(const char* json, const char* key, char* out, size_t out_size)
{
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    const char* key_pos = std::strstr(json, key);
    if (key_pos == nullptr)
    {
        return false;
    }

    const char* value_start = key_pos + std::strlen(key);
    size_t i = 0;
    bool closed = false;
    for (size_t cursor = 0; value_start[cursor] != '\0'; ++cursor)
    {
        char current = value_start[cursor];
        if (current == '"')
        {
            closed = true;
            break;
        }

        if (current == '\\')
        {
            ++cursor;
            current = value_start[cursor];
            if (current == '\0')
            {
                return false;
            }
        }

        if (i + 1 >= out_size)
        {
            return false;
        }

        out[i++] = current;
    }
    out[i] = '\0';

    return closed && i > 0;
}

bool extract_json_scalar_value(const char* json, const char* key, char* out, size_t out_size)
{
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    out[0] = '\0';

    const char* key_pos = std::strstr(json, key);
    if (key_pos == nullptr)
    {
        return false;
    }

    const char* value = key_pos + std::strlen(key);
    while (*value == ' ' || *value == '\t')
    {
        ++value;
    }

    if (*value == '\0' || *value == '"' || *value == '[' || *value == '{')
    {
        return false;
    }

    size_t i = 0;
    bool terminated = false;
    while (value[i] != '\0' && value[i] != ',' && value[i] != '}' && value[i] != ']' &&
           value[i] != '\r' && value[i] != '\n' && i + 1 < out_size)
    {
        out[i] = value[i];
        ++i;
    }

    if (value[i] == ',' || value[i] == '}' || value[i] == ']' || value[i] == '\r' ||
        value[i] == '\n')
    {
        terminated = true;
    }

    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t'))
    {
        --i;
    }
    out[i] = '\0';
    return terminated && i > 0;
}

const char* find_matching_json_object_end(const char* object_start)
{
    if (object_start == nullptr || *object_start != '{')
    {
        return nullptr;
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    for (const char* p = object_start; *p != '\0'; ++p)
    {
        const char current = *p;
        if (escape)
        {
            escape = false;
            continue;
        }

        if (current == '\\' && in_string)
        {
            escape = true;
            continue;
        }

        if (current == '"')
        {
            in_string = !in_string;
            continue;
        }

        if (in_string)
        {
            continue;
        }

        if (current == '{')
        {
            ++depth;
        }
        else if (current == '}')
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

const char* find_json_array_start(const char* json, const char* key)
{
    if (json == nullptr || key == nullptr)
    {
        return nullptr;
    }

    const char* key_pos = std::strstr(json, key);
    if (key_pos == nullptr)
    {
        return nullptr;
    }

    const char* array_start = std::strchr(key_pos, '[');
    return array_start != nullptr ? (array_start + 1) : nullptr;
}

bool json_array_string_at(const char* array, size_t index, char* out, size_t out_size)
{
    if (array == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    size_t current_index = 0;
    const char* cursor = array;
    while (*cursor != '\0' && *cursor != ']')
    {
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t' ||
               *cursor == ',')
        {
            ++cursor;
        }

        if (*cursor != '"')
        {
            return false;
        }
        ++cursor;

        const char* value_start = cursor;
        while (*cursor != '\0' && *cursor != '"')
        {
            ++cursor;
        }
        if (*cursor != '"')
        {
            return false;
        }

        if (current_index == index)
        {
            const size_t value_len = static_cast<size_t>(cursor - value_start);
            if (value_len + 1 > out_size)
            {
                return false;
            }
            std::memcpy(out, value_start, value_len);
            out[value_len] = '\0';
            return true;
        }

        ++current_index;
        ++cursor;
    }

    return false;
}

bool json_array_number_at(const char* array, size_t index, char* out, size_t out_size)
{
    if (array == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    size_t current_index = 0;
    const char* cursor = array;
    while (*cursor != '\0' && *cursor != ']')
    {
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t' ||
               *cursor == ',')
        {
            ++cursor;
        }

        const char* value_start = cursor;
        while (*cursor != '\0' && *cursor != ',' && *cursor != ']')
        {
            ++cursor;
        }

        if (value_start == cursor)
        {
            return false;
        }

        if (current_index == index)
        {
            const size_t value_len = static_cast<size_t>(cursor - value_start);
            if (value_len + 1 > out_size)
            {
                return false;
            }
            std::memcpy(out, value_start, value_len);
            out[value_len] = '\0';
            return true;
        }

        ++current_index;
    }

    return false;
}

bool open_meteo_daily_scalar(const char* daily_section, const char* primary_key,
                             const char* legacy_key, size_t index, char* out, size_t out_size,
                             bool string_value)
{
    if (daily_section == nullptr || primary_key == nullptr || out == nullptr || out_size == 0)
    {
        return false;
    }

    const char* array = find_json_array_start(daily_section, primary_key);
    if (array == nullptr && legacy_key != nullptr)
    {
        array = find_json_array_start(daily_section, legacy_key);
    }
    if (array == nullptr)
    {
        return false;
    }

    return string_value ? json_array_string_at(array, index, out, out_size)
                        : json_array_number_at(array, index, out, out_size);
}

} // namespace weather_json
