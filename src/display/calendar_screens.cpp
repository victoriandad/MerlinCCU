#include "calendar_screens.h"

#include <array>
#include <cstdint>
#include <cstdio>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

#if __has_include("calendar_identities.h")
#include "calendar_identities.h"
#else
#include "calendar_identities.example.h"
#endif

namespace calendar_screens
{

namespace
{

/// @brief Returns the owner label used by Calendar labels and details.
const char* calendar_owner_text(CalendarOwner owner)
{
    switch (owner)
    {
    case CalendarOwner::Combined:
        return "Combined";
    case CalendarOwner::Owner1:
        return kLocalCalendarIdentities[0][0] != '\0' ? kLocalCalendarIdentities[0] : "Owner 1";
    case CalendarOwner::Owner2:
        return kLocalCalendarIdentities[1][0] != '\0' ? kLocalCalendarIdentities[1] : "Owner 2";
    case CalendarOwner::Owner3:
        return kLocalCalendarIdentities[2][0] != '\0' ? kLocalCalendarIdentities[2] : "Owner 3";
    case CalendarOwner::Owner4:
        return kLocalCalendarIdentities[3][0] != '\0' ? kLocalCalendarIdentities[3] : "Owner 4";
    case CalendarOwner::Owner5:
        return kLocalCalendarIdentities[4][0] != '\0' ? kLocalCalendarIdentities[4] : "Owner 5";
    case CalendarOwner::Owner6:
        return kLocalCalendarIdentities[5][0] != '\0' ? kLocalCalendarIdentities[5] : "Owner 6";
    case CalendarOwner::Owner7:
        return kLocalCalendarIdentities[6][0] != '\0' ? kLocalCalendarIdentities[6] : "Owner 7";
    case CalendarOwner::Owner8:
        return kLocalCalendarIdentities[7][0] != '\0' ? kLocalCalendarIdentities[7] : "Owner 8";
    }

    return "Combined";
}

/// @brief Returns the short weekday label for compact Calendar date text.
const char* weekday_short_text(uint8_t weekday_index)
{
    static constexpr std::array<const char*, 7> kWeekdayLabels = {"Sun", "Mon", "Tue", "Wed",
                                                                  "Thu", "Fri", "Sat"};
    if (weekday_index >= kWeekdayLabels.size())
    {
        return "";
    }

    return kWeekdayLabels[weekday_index];
}

/// @brief Returns the weekday index reached by moving relative to today.
uint8_t shifted_weekday_index(uint8_t today_weekday_index, int8_t day_offset)
{
    int index = static_cast<int>(today_weekday_index) + static_cast<int>(day_offset);
    while (index < 0)
    {
        index += 7;
    }

    return static_cast<uint8_t>(index % 7);
}

/// @brief Returns the human-readable prefix for a week-distance bucket.
const char* calendar_week_prefix(int weeks)
{
    switch (weeks)
    {
    case 1:
        return "Week";
    case 2:
        return "Two Weeks";
    case 3:
        return "Three Weeks";
    case 4:
        return "Four Weeks";
    }

    return "Weeks";
}

/// @brief Formats a single relative day label for the Calendar footer.
/// @details The wording is deliberately compact because this label lives in
/// the narrow bottom footer, not in the surrounding event softkeys.
const char* calendar_day_text(const ConsoleState& console_state, int8_t day_offset, char* buffer,
                              size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    if (day_offset == 0)
    {
        return "Today";
    }
    if (day_offset == 1)
    {
        return "Tomorrow";
    }
    if (day_offset == -1)
    {
        return "Yesterday";
    }

    if (console_state.time_status.synced &&
        console_state.time_status.weekday_index != kInvalidWeekdayIndex)
    {
        const uint8_t weekday =
            shifted_weekday_index(console_state.time_status.weekday_index, day_offset);
        const char* weekday_text = weekday_short_text(weekday);
        if (day_offset < 0)
        {
            const int age_days = -static_cast<int>(day_offset);
            if (age_days < 7)
            {
                std::snprintf(buffer, buffer_size, "Last %s", weekday_text);
                return buffer;
            }

            std::snprintf(buffer, buffer_size, "%s Last %s", calendar_week_prefix(age_days / 7),
                          weekday_text);
            return buffer;
        }

        if (day_offset < 7)
        {
            std::snprintf(buffer, buffer_size, "%s", weekday_text);
            return buffer;
        }

        const int weeks = static_cast<int>(day_offset) / 7;
        const int remainder = static_cast<int>(day_offset) % 7;
        const char* relative_text = weekday_text;
        if (remainder == 0)
        {
            relative_text = "Today";
        }
        else if (remainder == 1)
        {
            relative_text = "Tomorrow";
        }

        std::snprintf(buffer, buffer_size, "%s %s", calendar_week_prefix(weeks), relative_text);
        return buffer;
    }

    if (day_offset > 0)
    {
        std::snprintf(buffer, buffer_size, "%d days", static_cast<int>(day_offset));
        return buffer;
    }

    std::snprintf(buffer, buffer_size, "%d days", static_cast<int>(day_offset));
    return buffer;
}

/// @brief Draws a small left or right arrow independent of font glyph support.
void draw_calendar_footer_arrow(uint8_t* fb, int centre_x, int centre_y, int direction)
{
    constexpr int kArrowLength = 11;
    constexpr int kArrowHead = 4;
    const int tip_x = centre_x + ((direction < 0) ? -kArrowLength / 2 : kArrowLength / 2);
    const int tail_x = centre_x + ((direction < 0) ? kArrowLength / 2 : -kArrowLength / 2);

    framebuffer::draw_hline(fb, tail_x, tip_x, centre_y, true);
    framebuffer::draw_line(fb, tip_x, centre_y, tip_x - (direction * kArrowHead),
                           centre_y - kArrowHead, true);
    framebuffer::draw_line(fb, tip_x, centre_y, tip_x - (direction * kArrowHead),
                           centre_y + kArrowHead, true);
}

/// @brief Draws the bottom Calendar relative-day footer.
/// @details The bottom cursor keys move the selected day, so the footer keeps
/// the active day bracketed by visible left/right arrow markers.
void draw_calendar_navigation_footer(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kFooterY = kUiHeight - 25;
    constexpr int kArrowGap = 12;
    constexpr int kArrowCentreYOffset = 3;
    constexpr fonts::FontFace kFooterFont = fonts::FontFace::Font5x7;
    char day_label[32] = {};
    const char* day_text = calendar_day_text(console_state, console_state.calendar_day_offset,
                                             day_label, sizeof(day_label));
    if (day_text == nullptr || day_text[0] == '\0')
    {
        std::snprintf(day_label, sizeof(day_label), "%d days",
                      static_cast<int>(console_state.calendar_day_offset));
        day_text = day_label;
    }

    const int label_width = screens::text_width(day_text, kFooterFont, 1);
    const int label_x = (kUiWidth - label_width) / 2;
    framebuffer::draw_text(fb, label_x, kFooterY, day_text, true, kFooterFont, 1);
    draw_calendar_footer_arrow(fb, label_x - kArrowGap, kFooterY + kArrowCentreYOffset, -1);
    draw_calendar_footer_arrow(fb, label_x + label_width + kArrowGap,
                               kFooterY + kArrowCentreYOffset, 1);
}

} // namespace

/// @brief Draws the shared calendar overview selected from Home.
/// @details Event summaries live on the surrounding softkeys. The centre area
/// deliberately stays blank so the page remains a label-driven CCU view.
void draw_calendar_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_calendar_navigation_footer(fb, console_state);
}

/// @brief Draws detailed data for the selected shared calendar event.
/// @details The fields mirror upstream calendar properties that are useful on
/// a small operational display: time, owner, location, reminders, attendees,
/// and free-form description text.
void draw_calendar_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.selected_calendar_event_index >= console_state.calendar_events.size())
    {
        screens::draw_centered_text(fb, kUiWidth / 2, 112, "NO EVENT", true,
                                    fonts::FontFace::Font8x12, 1);
        return;
    }

    const CalendarEvent& event =
        console_state.calendar_events[console_state.selected_calendar_event_index];
    if (event.title[0] == '\0')
    {
        screens::draw_centered_text(fb, kUiWidth / 2, 112, "NO EVENT", true,
                                    fonts::FontFace::Font8x12, 1);
        return;
    }

    char owner_line[48] = {};
    char time_line[48] = {};
    std::snprintf(owner_line, sizeof(owner_line), "%s", calendar_owner_text(event.owner));
    std::snprintf(time_line, sizeof(time_line), "%s-%s",
                  event.start_time[0] != '\0' ? event.start_time.data() : "--:--",
                  event.end_time[0] != '\0' ? event.end_time.data() : "--:--");

    const screens::DetailRow rows[] = {
        {"Event", event.title.data()},
        {"Time", time_line},
        {"Owner", owner_line},
        {"Location", event.location[0] != '\0' ? event.location.data() : "-"},
        {"Alarm", event.reminder[0] != '\0' ? event.reminder.data() : "-"},
        {"Attendees", event.attendees[0] != '\0' ? event.attendees.data() : "-"},
        {"Detail", event.description[0] != '\0' ? event.description.data() : "-"},
    };
    constexpr int kStartY = 42;
    constexpr int kRowPitch = 18;
    constexpr size_t kRowCount = sizeof(rows) / sizeof(rows[0]);

    screens::draw_compact_detail_rows(fb, rows, kRowCount, kStartY, kRowPitch);
}

} // namespace calendar_screens
