#include "home_controls.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "weather_json_scan.h"

namespace home_controls
{

namespace
{

constexpr std::string_view kListSuffix = "/controls/list";
constexpr std::string_view kResultSuffix = "/controls/result";
// Longest array string read; a name over kNameLength is cut to it.
constexpr size_t kMaxStringBytes = 64U;

/// @brief Reads an unsigned integer field such as `"v":1`.
std::optional<uint32_t> read_unsigned(const char* json, const char* key)
{
    char text[12] = {};
    if (!weather_json::extract_json_scalar_value(json, key, text, sizeof(text)))
    {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0')
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(value);
}

/// @brief Counts the strings in `array` (just past its `[`); nothing when the
/// array is missing. Stops one past kMaxControls, so a long list is refused.
std::optional<size_t> string_array_length(const char* array)
{
    if (array == nullptr)
    {
        return std::nullopt;
    }
    char scratch[kMaxStringBytes] = {};
    size_t count = 0U;
    while (count <= kMaxControls &&
           weather_json::json_array_string_at(array, count, scratch, sizeof(scratch)))
    {
        ++count;
    }
    return count;
}

} // namespace

std::optional<Topic> topic_for(std::string_view topic, std::string_view prefix)
{
    if (prefix.empty() || topic.size() <= prefix.size() || topic.substr(0, prefix.size()) != prefix)
    {
        return std::nullopt;
    }
    const std::string_view rest = topic.substr(prefix.size());
    if (rest == kListSuffix)
    {
        return Topic::List;
    }
    if (rest == kResultSuffix)
    {
        return Topic::Result;
    }
    return std::nullopt;
}

bool apply_list(const char* json, HomeControls& controls)
{
    if (json == nullptr || read_unsigned(json, "\"v\":") != kSchemaVersion)
    {
        return false;
    }
    char list_id[kListIdLength + 1U] = {};
    if (!weather_json::extract_json_string_value(json, "\"list\":\"", list_id, sizeof(list_id)))
    {
        return false;
    }
    const char* names = weather_json::find_json_array_start(json, "\"names\":");
    const char* kinds = weather_json::find_json_array_start(json, "\"kinds\":");
    const char* states = weather_json::find_json_array_start(json, "\"states\":");
    const std::optional<size_t> count = string_array_length(names);
    if (!count.has_value() || *count > kMaxControls || string_array_length(kinds) != count ||
        string_array_length(states) != count)
    {
        return false;
    }

    std::array<HomeControl, kMaxControls> items = {};
    for (size_t i = 0U; i < *count; ++i)
    {
        char text[kMaxStringBytes] = {};
        (void)weather_json::json_array_string_at(names, i, text, sizeof(text));
        std::snprintf(items[i].name.data(), items[i].name.size(), "%s", text);
        (void)weather_json::json_array_string_at(kinds, i, text, sizeof(text));
        items[i].kind = std::strcmp(text, "run") == 0 ? ControlKind::Run : ControlKind::Toggle;
        (void)weather_json::json_array_string_at(states, i, text, sizeof(text));
        items[i].state = std::strcmp(text, "on") == 0            ? ControlState::On
                         : std::strcmp(text, "unavailable") == 0 ? ControlState::Unavailable
                                                                 : ControlState::Off;
    }

    const bool same_list = controls.received && std::strcmp(list_id, controls.list_id.data()) == 0;
    if (!same_list)
    {
        controls.pending.reset();
        controls.press_queued = false;
        controls.failure = ControlFailure::None;
    }
    controls.received = true;
    controls.count = static_cast<uint8_t>(*count);
    controls.items = items;
    std::memcpy(controls.list_id.data(), list_id, sizeof(list_id));
    const uint8_t pages = page_count(controls);
    if (controls.page_index >= pages)
    {
        controls.page_index = static_cast<uint8_t>(pages - 1U);
    }
    return true;
}

bool apply_result(const char* json, HomeControls& controls, uint32_t now_ms)
{
    if (json == nullptr || read_unsigned(json, "\"v\":") != kSchemaVersion)
    {
        return false;
    }
    const std::optional<uint32_t> index = read_unsigned(json, "\"index\":");
    if (!index.has_value() || !controls.pending.has_value() ||
        *index != *controls.pending)
    {
        return false;
    }
    char ok[8] = {};
    (void)weather_json::extract_json_scalar_value(json, "\"ok\":", ok, sizeof(ok));
    HomeControl& item = controls.items[*index];
    if (std::strcmp(ok, "true") == 0)
    {
        if (item.kind == ControlKind::Toggle && item.state == controls.pending_from &&
            item.state != ControlState::Unavailable)
        {
            item.state = item.state == ControlState::On ? ControlState::Off : ControlState::On;
        }
    }
    else
    {
        controls.failure = ControlFailure::Refused;
        controls.failure_index = static_cast<uint8_t>(*index);
        controls.failure_at_ms = now_ms;
        ++controls.failure_count;
    }
    controls.pending.reset();
    return true;
}

bool press(HomeControls& controls, uint8_t index, uint32_t now_ms)
{
    if (!controls.link_up || controls.pending.has_value() || index >= controls.count ||
        controls.items[index].state == ControlState::Unavailable)
    {
        return false;
    }
    controls.pending = index;
    controls.pending_since_ms = now_ms;
    controls.pending_from = controls.items[index].state;
    controls.press_queued = true;
    controls.press_index = index;
    controls.failure = ControlFailure::None;
    return true;
}

bool take_press(HomeControls& controls, char* out, size_t out_size)
{
    if (!controls.press_queued || out == nullptr)
    {
        return false;
    }
    const int length = std::snprintf(out, out_size, "%s:%u", controls.list_id.data(),
                                     static_cast<unsigned>(controls.press_index));
    if (length <= 0 || static_cast<size_t>(length) >= out_size)
    {
        return false;
    }
    controls.press_queued = false;
    return true;
}

bool set_link(HomeControls& controls, bool link_up)
{
    if (controls.link_up == link_up)
    {
        return false;
    }
    controls.link_up = link_up;
    if (!link_up)
    {
        controls.pending.reset();
        controls.press_queued = false;
    }
    return true;
}

bool tick(HomeControls& controls, uint32_t now_ms)
{
    bool changed = false;
    if (controls.pending.has_value() && now_ms - controls.pending_since_ms >= kReplyTimeoutMs)
    {
        controls.failure = ControlFailure::NoReply;
        controls.failure_index = *controls.pending;
        controls.failure_at_ms = now_ms;
        ++controls.failure_count;
        controls.pending.reset();
        controls.press_queued = false;
        changed = true;
    }
    if (controls.failure != ControlFailure::None &&
        now_ms - controls.failure_at_ms >= kFailureNoticeMs)
    {
        controls.failure = ControlFailure::None;
        changed = true;
    }
    return changed;
}

uint8_t page_count(const HomeControls& controls)
{
    const size_t pages = (static_cast<size_t>(controls.count) + kControlsPerPage - 1U) /
                         kControlsPerPage;
    return static_cast<uint8_t>(pages == 0U ? 1U : pages);
}

void failure_notice(const HomeControls& controls, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    out[0] = '\0';
    if (controls.failure == ControlFailure::None || controls.failure_index >= controls.count)
    {
        return;
    }
    char name[kNameLength + 1U] = {};
    const char* source = controls.items[controls.failure_index].name.data();
    for (size_t i = 0U; i < kNameLength && source[i] != '\0'; ++i)
    {
        name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(source[i])));
    }
    std::snprintf(out, out_size, "%s: %s", name,
                  controls.failure == ControlFailure::Refused ? "HA REFUSED" : "NO REPLY FROM HA");
}

} // namespace home_controls
