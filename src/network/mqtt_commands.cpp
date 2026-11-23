#include "mqtt_commands.h"

#include <cstdio>
#include <cstring>

#include "bounded_json.h"

namespace mqtt_commands
{

namespace
{

constexpr std::string_view kSetSuffix = "/set";

constexpr std::array<TargetInfo, static_cast<size_t>(Target::Count)> kTargets = {{
    {"alert_lamp", "ALRT lamp", "select", "mdi:alarm-light"},
    {"test_lamp", "TEST lamp", "select", "mdi:lightbulb"},
    {"panel_brightness", "Panel brightness", "select", "mdi:brightness-6"},
    {"key_backlight", "Key backlight", "select", "mdi:keyboard-outline"},
    {"display", "Display", "select", "mdi:monitor"},
    {"page", "Page", "select", "mdi:page-next-outline"},
    {"message", "Message", "notify", "mdi:message-alert"},
}};

constexpr std::array<const char*, 5> kLampOptions = {"auto", "off", "on", "flash_slow",
                                                     "flash_fast"};
constexpr std::array<const char*, 4> kBrightnessOptions = {"off", "low", "medium", "high"};
constexpr std::array<const char*, 2> kDisplayOptions = {"awake", "screen_saver"};

/// @brief Pages HA can open, in option order.
struct PageOption
{
    const char* name;
    MenuPage page;
};
constexpr std::array<PageOption, 8> kPageOptions = {{
    {"home", MenuPage::Home},
    {"weather", MenuPage::Weather},
    {"calendar", MenuPage::Calendar},
    {"status", MenuPage::Status},
    {"alerts", MenuPage::AlertList},
    {"shares", MenuPage::Shares},
    {"pinter", MenuPage::Pinter},
    {"local_conditions", MenuPage::LocalConditions},
}};

/// @brief Finds a name in an option list.
template <size_t N>
std::optional<uint8_t> find_option(const std::array<const char*, N>& options, std::string_view name)
{
    for (size_t i = 0U; i < N; ++i)
    {
        if (name == options[i])
        {
            return static_cast<uint8_t>(i);
        }
    }
    return std::nullopt;
}

/// @brief Drops spaces, tabs and line ends from both ends.
std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n'))
    {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n'))
    {
        text.remove_suffix(1U);
    }
    return text;
}

/// @brief Copies text, turning control characters into spaces so a message
/// cannot break the panel's line layout.
template <size_t N> void copy_printable(std::array<char, N>& out, std::string_view text)
{
    out.fill('\0');
    const size_t length = text.size() < N - 1U ? text.size() : N - 1U;
    for (size_t i = 0U; i < length; ++i)
    {
        const char c = text[i];
        out[i] = (c == '\n' || (c >= ' ' && c != 0x7F)) ? c : ' ';
    }
}

std::optional<Command> parse_message(std::string_view payload)
{
    Command command;
    command.target = Target::Message;
    payload = trim(payload);

    if (payload.empty() || payload.front() != '{')
    {
        if (payload.size() > kMaxMessageLength)
        {
            return std::nullopt;
        }
        copy_printable(command.message.text, payload);
        return command;
    }

    // JSON: the bounded scanner needs a terminated copy.
    std::array<char, kMaxPayloadLength + 1U> json = {};
    std::memcpy(json.data(), payload.data(), payload.size());
    const char* begin = json.data();
    const char* end = begin + payload.size();

    std::array<char, kMaxMessageLength + 2U> text = {};
    if (!bounded_json::extract_bounded_string(begin, end, "\"message\"", text.data(), text.size()))
    {
        return std::nullopt;
    }
    if (std::strlen(text.data()) > kMaxMessageLength)
    {
        return std::nullopt;
    }
    copy_printable(command.message.text, text.data());

    std::array<char, kMaxTitleLength + 2U> title = {};
    if (bounded_json::extract_bounded_string(begin, end, "\"title\"", title.data(), title.size()))
    {
        if (std::strlen(title.data()) > kMaxTitleLength)
        {
            return std::nullopt;
        }
        copy_printable(command.message.title, title.data());
    }

    std::array<char, 16> severity = {};
    if (bounded_json::extract_bounded_string(begin, end, "\"severity\"", severity.data(),
                                             severity.size()))
    {
        const std::string_view name = severity.data();
        if (name == "message")
        {
            command.message.severity = AlertSeverity::Message;
        }
        else if (name == "warning")
        {
            command.message.severity = AlertSeverity::Warning;
        }
        else if (name == "alert")
        {
            command.message.severity = AlertSeverity::Alert;
        }
        else
        {
            return std::nullopt;
        }
    }
    return command;
}

} // namespace

const TargetInfo& info(Target target)
{
    const size_t index = static_cast<size_t>(target);
    return kTargets[index < kTargets.size() ? index : 0U];
}

const char* options_json(Target target)
{
    switch (target)
    {
    case Target::AlertLamp:
    case Target::TestLamp:
        return "[\"auto\",\"off\",\"on\",\"flash_slow\",\"flash_fast\"]";
    case Target::PanelBrightness:
    case Target::KeyBacklight:
        return "[\"off\",\"low\",\"medium\",\"high\"]";
    case Target::Display:
        return "[\"awake\",\"screen_saver\"]";
    case Target::Page:
        return "[\"home\",\"weather\",\"calendar\",\"status\",\"alerts\",\"shares\",\"pinter\","
               "\"local_conditions\"]";
    case Target::Message:
    case Target::Count:
        break;
    }
    return nullptr;
}

const char* option_name(Target target, uint8_t value)
{
    switch (target)
    {
    case Target::AlertLamp:
    case Target::TestLamp:
        return value < kLampOptions.size() ? kLampOptions[value] : nullptr;
    case Target::PanelBrightness:
    case Target::KeyBacklight:
        return value < kBrightnessOptions.size() ? kBrightnessOptions[value] : nullptr;
    case Target::Display:
        return value < kDisplayOptions.size() ? kDisplayOptions[value] : nullptr;
    case Target::Page:
        return value < kPageOptions.size() ? kPageOptions[value].name : nullptr;
    case Target::Message:
    case Target::Count:
        break;
    }
    return nullptr;
}

MenuPage page_for(uint8_t value)
{
    return value < kPageOptions.size() ? kPageOptions[value].page : MenuPage::Home;
}

std::optional<uint8_t> page_option(MenuPage page)
{
    for (size_t i = 0U; i < kPageOptions.size(); ++i)
    {
        if (kPageOptions[i].page == page)
        {
            return static_cast<uint8_t>(i);
        }
    }
    return std::nullopt;
}

std::optional<Target> target_for_topic(std::string_view topic, std::string_view prefix)
{
    if (topic.size() <= prefix.size() + 1U + kSetSuffix.size() ||
        topic.substr(0U, prefix.size()) != prefix || topic[prefix.size()] != '/' ||
        topic.substr(topic.size() - kSetSuffix.size()) != kSetSuffix)
    {
        return std::nullopt;
    }
    const std::string_view slug =
        topic.substr(prefix.size() + 1U, topic.size() - prefix.size() - 1U - kSetSuffix.size());
    for (size_t i = 0U; i < kTargets.size(); ++i)
    {
        if (slug == kTargets[i].slug)
        {
            return static_cast<Target>(i);
        }
    }
    return std::nullopt;
}

std::optional<Command> parse(Target target, std::string_view payload)
{
    if (payload.size() > kMaxPayloadLength)
    {
        return std::nullopt;
    }
    if (target == Target::Message)
    {
        return parse_message(payload);
    }

    const std::string_view name = trim(payload);
    std::optional<uint8_t> value;
    switch (target)
    {
    case Target::AlertLamp:
    case Target::TestLamp:
        value = find_option(kLampOptions, name);
        break;
    case Target::PanelBrightness:
    case Target::KeyBacklight:
        value = find_option(kBrightnessOptions, name);
        break;
    case Target::Display:
        value = find_option(kDisplayOptions, name);
        break;
    case Target::Page:
        for (size_t i = 0U; i < kPageOptions.size(); ++i)
        {
            if (name == kPageOptions[i].name)
            {
                value = static_cast<uint8_t>(i);
            }
        }
        break;
    case Target::Message:
    case Target::Count:
        break;
    }
    if (!value.has_value())
    {
        return std::nullopt;
    }
    Command command;
    command.target = target;
    command.value = *value;
    return command;
}

} // namespace mqtt_commands
