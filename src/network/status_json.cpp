#include "status_json.h"

#include <cstdio>

namespace status_json
{

namespace
{

/// @brief Appends to a fixed buffer, remembering whether anything was cut.
class Writer
{
  public:
    Writer(char* out, size_t size) : out_(out), size_(size) {}

    void raw(const char* text)
    {
        for (; text != nullptr && *text != '\0'; ++text)
        {
            put(*text);
        }
    }

    /// @brief A JSON string: quotes, `"` and `\` escaped, control characters
    /// written as \u00XX.
    void string(const char* text)
    {
        put('"');
        for (; text != nullptr && *text != '\0'; ++text)
        {
            const auto c = static_cast<unsigned char>(*text);
            if (c == '"' || c == '\\')
            {
                put('\\');
                put(static_cast<char>(c));
            }
            else if (c < 0x20U)
            {
                char escaped[8] = {};
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                raw(escaped);
            }
            else
            {
                put(static_cast<char>(c));
            }
        }
        put('"');
    }

    void number(long value)
    {
        char digits[16] = {};
        std::snprintf(digits, sizeof(digits), "%ld", value);
        raw(digits);
    }

    void boolean(bool value)
    {
        raw(value ? "true" : "false");
    }

    /// @brief Starts `"name":`, after a comma unless it is the first field.
    void field(const char* name)
    {
        if (fields_++ > 0U)
        {
            put(',');
        }
        string(name);
        put(':');
    }

    bool finish()
    {
        if (used_ >= size_ || overflow_)
        {
            return false;
        }
        out_[used_] = '\0';
        return true;
    }

  private:
    void put(char c)
    {
        if (used_ + 1U >= size_)
        {
            overflow_ = true;
            return;
        }
        out_[used_++] = c;
    }

    char* out_;
    size_t size_;
    size_t used_ = 0U;
    unsigned fields_ = 0U;
    bool overflow_ = false;
};

} // namespace

bool build(const Status& status, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return false;
    }
    Writer json(out, out_size);
    json.raw("{");
    json.field("unit");
    json.string(status.unit);
    json.field("label");
    json.string(status.label);
    json.field("firmware");
    json.string(status.firmware);
    json.field("board");
    json.string(status.board);
    json.field("uptime_s");
    json.number(static_cast<long>(status.uptime_s));
    json.field("wifi_connected");
    json.boolean(status.wifi_connected);
    json.field("rssi");
    if (status.rssi_dbm.has_value())
    {
        json.number(*status.rssi_dbm);
    }
    else
    {
        json.raw("null");
    }
    json.field("ha_connected");
    json.boolean(status.ha_connected);
    json.field("mqtt_connected");
    json.boolean(status.mqtt_connected);
    json.field("free_heap");
    json.number(static_cast<long>(status.free_heap));
    json.field("current_page");
    json.string(status.current_page);
    json.field("alerts_active");
    json.number(status.alerts_active);
    json.field("top_alert");
    json.string(status.top_alert);
    json.field("units_online");
    json.number(status.units_online);
    json.field("units_known");
    json.number(status.units_known);
    json.raw("}\n");
    if (!json.finish())
    {
        std::snprintf(out, out_size, "{}\n");
        return false;
    }
    return true;
}

} // namespace status_json
