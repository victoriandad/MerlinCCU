#include "weather_acks.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "weather_json_scan.h"

namespace weather_acks
{

namespace
{

constexpr const char* kNames[kKindCount] = {"official", "wx_provider", "wx_temp", "wx_wind"};
constexpr uint32_t kSchemaVersion = 1U;

/// @brief CRC-32 (IEEE), bit by bit: ids are made a few times a second at
/// most, so no table is kept.
uint32_t crc32_update(uint32_t crc, std::string_view text)
{
    for (const char c : text)
    {
        crc ^= static_cast<uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

bool is_id(const char* text)
{
    if (std::strlen(text) != kIdLength)
    {
        return false;
    }
    for (size_t i = 0U; i < kIdLength; ++i)
    {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

} // namespace

const char* name(Kind kind)
{
    const auto index = static_cast<size_t>(kind);
    return index < kKindCount ? kNames[index] : "";
}

Id id_for(std::string_view key, std::string_view more)
{
    uint32_t crc = crc32_update(0xFFFFFFFFU, key);
    if (!more.empty())
    {
        crc = crc32_update(crc32_update(crc, "\n"), more);
    }
    Id id = {};
    std::snprintf(id.data(), id.size(), "%08lx", static_cast<unsigned long>(~crc));
    return id;
}

bool apply(const char* json, Accepted& accepted)
{
    char version[8] = {};
    if (json == nullptr ||
        !weather_json::extract_json_scalar_value(json, "\"v\":", version, sizeof(version)) ||
        std::strtoul(version, nullptr, 10) != kSchemaVersion)
    {
        return false;
    }
    Accepted updated = {};
    for (size_t i = 0U; i < kKindCount; ++i)
    {
        char key[24] = {};
        std::snprintf(key, sizeof(key), "\"%s\":\"", kNames[i]);
        char value[16] = {};
        if (weather_json::extract_json_string_value(json, key, value, sizeof(value)) &&
            is_id(value))
        {
            std::memcpy(updated[i].data(), value, kIdLength + 1U);
        }
    }
    accepted = updated;
    return true;
}

bool format_ack(Kind kind, const Id& id, const char* unit, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return false;
    }
    // The unit name is a device name: letters, digits, - and _, so it needs
    // no escaping.
    const int length =
        std::snprintf(out, out_size, "{\"alert\":\"%s\",\"id\":\"%s\",\"unit\":\"%s\"}", name(kind),
                      id.data(), unit != nullptr ? unit : "");
    return length > 0 && static_cast<size_t>(length) < out_size;
}

} // namespace weather_acks
